#!/usr/bin/env python

"""
Spawn hero vehicle on ALL nodes AND synchronize their state across DisplayCluster nodes.

This integrated script:
1. Spawns identical hero vehicles on all nodes (master + slaves)
2. Syncs vehicle transforms and velocity frame-by-frame
3. Keeps all vehicle copies in perfect sync for rendering

Run this ONCE before using manual_control.py
"""

import carla
import random
import time
import argparse

class NDDisplayVehicleSync:
    def __init__(self, master_host, master_port, slave_hosts, slave_port, spawn_index=0):
        """
        Initialize synchronizer
        
        Args:
            master_host: IP of master node (where controls are applied)
            master_port: CARLA port on master
            slave_hosts: List of slave node IPs
            slave_port: CARLA port on slaves
            spawn_index: Which spawn point to use (default: 0)
        """
        self.master_host = master_host
        self.master_port = master_port
        self.slave_hosts = slave_hosts
        self.slave_port = slave_port
        self.spawn_index = spawn_index
        
        self.master_client = None
        self.master_world = None
        self.slave_clients = {}
        self.slave_worlds = {}
        self.hero_vehicle_master = None
        self.hero_vehicles_slave = {}
        self.frame_count = 0
        self.sync_errors = 0
        self.spawn_errors = 0
        
        # Connect to master
        try:
            print(f'Connecting to master node {master_host}:{master_port}...')
            self.master_client = carla.Client(master_host, master_port)
            self.master_client.set_timeout(10.0)
            self.master_world = self.master_client.get_world()
            print(f'✓ Connected to master')
        except Exception as e:
            print(f'✗ FATAL: Failed to connect to master: {e}')
            raise
        
        # Connect to slaves
        for i, host in enumerate(slave_hosts):
            try:
                print(f'Connecting to slave node {i+1} {host}:{slave_port}...')
                client = carla.Client(host, slave_port)
                client.set_timeout(10.0)
                world = client.get_world()
                self.slave_clients[host] = client
                self.slave_worlds[host] = world
                print(f'✓ Connected to slave {i+1}')
            except Exception as e:
                print(f'✗ Failed to connect to slave {host}: {e}')

    
    def spawn_hero_vehicles(self):
        """Spawn identical hero vehicles on all nodes"""
        print('\n' + '='*70)
        print('SPAWNING HERO VEHICLES ON ALL NODES')
        print('='*70)
        
        # Get spawn points from master
        spawn_points = self.master_world.get_map().get_spawn_points()
        if not spawn_points:
            print('✗ ERROR: No spawn points found in map!')
            return False
        
        if self.spawn_index >= len(spawn_points):
            print(f'✗ ERROR: Spawn index {self.spawn_index} out of range (max: {len(spawn_points)-1})')
            return False
        
        spawn_point = spawn_points[self.spawn_index]
        blueprint_id = 'vehicle.tesla.model3'
        spawned = 0
        
        # Spawn on MASTER
        try:
            print(f'\nSpawning on master {self.master_host}...')
            bp_lib = self.master_world.get_blueprint_library()
            vehicle_bp = bp_lib.find(blueprint_id)
            if not vehicle_bp:
                vehicle_bp = random.choice(bp_lib.filter('vehicle.*'))
            
            vehicle_bp.set_attribute('role_name', 'hero')
            if vehicle_bp.has_attribute('color'):
                color = random.choice(vehicle_bp.get_attribute('color').recommended_values)
                vehicle_bp.set_attribute('color', color)
            
            self.hero_vehicle_master = self.master_world.try_spawn_actor(vehicle_bp, spawn_point)
            if self.hero_vehicle_master:
                print(f'✓ Spawned hero on master (ID: {self.hero_vehicle_master.id})')
                spawned += 1
            else:
                print(f'✗ Failed to spawn on master')
                self.spawn_errors += 1
        except Exception as e:
            print(f'✗ Error spawning on master: {e}')
            self.spawn_errors += 1
        
        # Spawn on SLAVES
        for host, world in self.slave_worlds.items():
            try:
                print(f'Spawning on slave {host}...')
                bp_lib = world.get_blueprint_library()
                vehicle_bp = bp_lib.find(blueprint_id)
                if not vehicle_bp:
                    vehicle_bp = random.choice(bp_lib.filter('vehicle.*'))
                
                vehicle_bp.set_attribute('role_name', 'hero')
                if vehicle_bp.has_attribute('color'):
                    color = random.choice(vehicle_bp.get_attribute('color').recommended_values)
                    vehicle_bp.set_attribute('color', color)
                
                slave_vehicle = world.try_spawn_actor(vehicle_bp, spawn_point)
                if slave_vehicle:
                    self.hero_vehicles_slave[host] = slave_vehicle
                    print(f'✓ Spawned hero on slave {host} (ID: {slave_vehicle.id})')
                    spawned += 1
                else:
                    print(f'✗ Failed to spawn on slave {host}')
                    self.spawn_errors += 1
            except Exception as e:
                print(f'✗ Error spawning on slave {host}: {e}')
                self.spawn_errors += 1
        
        print(f'\nSpawned {spawned} hero vehicles on {1 + len(self.slave_worlds)} nodes')
        
        return self.hero_vehicle_master is not None and len(self.hero_vehicles_slave) == len(self.slave_worlds)
    
    def find_hero_vehicles(self):
        """Find the hero vehicle on all nodes (after spawning)"""
        print('\nSearching for hero vehicles...')
        
        # Find on master
        actors = self.master_world.get_actors().filter('vehicle.*')
        for actor in actors:
            try:
                if actor.attributes.get('role_name') == 'hero':
                    self.hero_vehicle_master = actor
                    print(f'✓ Found hero vehicle on master (ID: {actor.id})')
                    break
            except:
                pass
        
        if not self.hero_vehicle_master:
            print('✗ Hero vehicle not found on master!')
            return False
        
        # Find on slaves
        for host, world in self.slave_worlds.items():
            actors = world.get_actors().filter('vehicle.*')
            for actor in actors:
                try:
                    if actor.attributes.get('role_name') == 'hero':
                        self.hero_vehicles_slave[host] = actor
                        print(f'✓ Found hero vehicle on slave {host} (ID: {actor.id})')
                        break
                except:
                    pass
        
        if len(self.hero_vehicles_slave) != len(self.slave_worlds):
            print(f'⚠ Found {len(self.hero_vehicles_slave)} out of {len(self.slave_worlds)} slave vehicles')
        
        return self.hero_vehicle_master is not None
    
    def sync_vehicle_state(self):
        """
        Read vehicle state from master and apply to all slaves
        This keeps all vehicles in perfect sync
        """
        if not self.hero_vehicle_master:
            return False
        
        try:
            # Get master vehicle state
            master_transform = self.hero_vehicle_master.get_transform()
            master_velocity = self.hero_vehicle_master.get_velocity()
            master_angular_velocity = self.hero_vehicle_master.get_angular_velocity()
            
            # Apply to all slave vehicles
            for host, slave_vehicle in self.hero_vehicles_slave.items():
                try:
                    # Teleport to exact location/rotation (instantaneous sync)
                    slave_vehicle.set_transform(master_transform)
                    
                    # Set constant velocity for physics continuity
                    # Use enable_constant_velocity instead of set_velocity (which doesn't exist in 0.9.15)
                    slave_vehicle.enable_constant_velocity(master_velocity)
                    
                except Exception as e:
                    self.sync_errors += 1
                    if self.sync_errors < 10:  # Only print first 10 errors
                        print(f'Error syncing to {host}: {e}')
            
            self.frame_count += 1
            
            # Print status every 30 frames
            if self.frame_count % 30 == 0:
                loc = master_transform.location
                speed = master_velocity.length()
                print(f'[Frame {self.frame_count}] Vehicle at ({loc.x:.1f}, {loc.y:.1f}, {loc.z:.1f}) - Speed: {speed:.1f} m/s')
            
            return True
        
        except Exception as e:
            print(f'Error reading master vehicle state: {e}')
            return False
    
    def run(self, duration=None, skip_spawn=False):
        """
        Run synchronization loop
        
        Args:
            duration: Max time to run in seconds (None = infinite)
            skip_spawn: If True, don't spawn vehicles (assume they exist)
        """
        # Step 1: Spawn vehicles if not skipping
        if not skip_spawn:
            if not self.spawn_hero_vehicles():
                print('\n✗ FATAL: Could not spawn hero vehicles on all nodes')
                return
            # Give a moment for actors to initialize
            time.sleep(0.5)
        
        # Step 2: Find vehicles
        if not self.find_hero_vehicles():
            print('✗ Cannot start sync - hero vehicles not found on all nodes')
            return
        
        print('\n' + '='*70)
        print('VEHICLE STATE SYNCHRONIZATION STARTED')
        print('='*70)
        print(f'Master vehicle will be synced to {len(self.hero_vehicles_slave)} slave nodes')
        print('Press Ctrl+C to stop\n')
        
        start_time = time.time()
        
        try:
            while True:
                # Sync the vehicle state
                if not self.sync_vehicle_state():
                    break
                
                # Small delay (sync runs at ~60 Hz)
                time.sleep(0.016)
                
                # Check duration
                if duration and (time.time() - start_time) > duration:
                    print(f'\nSync duration ({duration}s) reached.')
                    break
        
        except KeyboardInterrupt:
            print('\n\nSync stopped by user')
        
        finally:
            print('='*70)
            print(f'SYNC COMPLETE: {self.frame_count} frames synced')
            if self.sync_errors > 0:
                print(f'Errors encountered: {self.sync_errors}')
            print('='*70)

def main():
    argparser = argparse.ArgumentParser(
        description='Spawn hero vehicle on all nodes and synchronize state across DisplayCluster')
    
    argparser.add_argument(
        '--master',
        metavar='H',
        default='172.31.90.146',
        help='Master node IP (default: 172.31.90.146)')
    argparser.add_argument(
        '--slave1',
        metavar='H',
        default='172.31.89.47',
        help='Slave node 1 IP (default: 172.31.89.47)')
    argparser.add_argument(
        '--slave2',
        metavar='H',
        default='172.31.90.149',
        help='Slave node 2 IP (default: 172.31.90.149)')
    argparser.add_argument(
        '-p', '--port',
        metavar='P',
        default=2000,
        type=int,
        help='CARLA port (default: 2000)')
    argparser.add_argument(
        '--spawn-index',
        metavar='IDX',
        default=0,
        type=int,
        help='Spawn point index to use (default: 0)')
    argparser.add_argument(
        '--skip-spawn',
        action='store_true',
        help='Skip spawning (assume vehicles already exist on all nodes)')
    argparser.add_argument(
        '--duration',
        metavar='SEC',
        default=None,
        type=int,
        help='Run for N seconds then exit (default: infinite)')
    
    args = argparser.parse_args()
    
    slave_hosts = [args.slave1, args.slave2]
    
    sync = NDDisplayVehicleSync(
        args.master, args.port,
        slave_hosts, args.port,
        spawn_index=args.spawn_index
    )
    
    sync.run(duration=args.duration, skip_spawn=args.skip_spawn)

if __name__ == '__main__':    main()