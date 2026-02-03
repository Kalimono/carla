#!/usr/bin/env python

"""
Spawn hero vehicle on ALL DisplayCluster nodes simultaneously.
This ensures the same vehicle exists in each node's world for proper camera following.
"""

import carla
import random
import time
import argparse

def spawn_vehicle_on_node(host, port, spawn_index=0):
    """Spawn a hero vehicle on a specific CARLA instance"""
    try:
        print(f'\nConnecting to {host}:{port}...')
        client = carla.Client(host, port)
        client.set_timeout(10.0)
        
        world = client.get_world()
        print(f'Connected to {host} - Map: {world.get_map().name}')
        
        # Get blueprint library
        blueprint_library = world.get_blueprint_library()
        vehicle_blueprints = blueprint_library.filter('vehicle.tesla.model3')
        
        if not vehicle_blueprints:
            print(f'ERROR on {host}: No vehicles found')
            return None
        
        vehicle_bp = random.choice(vehicle_blueprints)
        
        # CRITICAL: Set role_name to 'hero' for HeroFollowerActor
        vehicle_bp.set_attribute('role_name', 'hero')
        
        # Set color
        if vehicle_bp.has_attribute('color'):
            color = random.choice(vehicle_bp.get_attribute('color').recommended_values)
            vehicle_bp.set_attribute('color', color)
        
        # Get spawn points
        spawn_points = world.get_map().get_spawn_points()
        if not spawn_points:
            print(f'ERROR on {host}: No spawn points found')
            return None
        
        # Use the same spawn point on all nodes for consistency
        spawn_point = spawn_points[spawn_index]
        
        print(f'Spawning hero vehicle on {host} at spawn point {spawn_index}...')
        vehicle = world.try_spawn_actor(vehicle_bp, spawn_point)
        
        if vehicle is None:
            print(f'ERROR on {host}: Failed to spawn vehicle')
            return None
        
        print(f'✓ Successfully spawned hero vehicle on {host}')
        print(f'  Vehicle ID: {vehicle.id}')
        print(f'  Location: {vehicle.get_location()}')
        
        return vehicle
    
    except Exception as e:
        print(f'ERROR on {host}: {str(e)}')
        return False

def main():
    argparser = argparse.ArgumentParser(description='Spawn Hero Vehicle on All DisplayCluster Nodes')
    argparser.add_argument(
        '--node1',
        metavar='H',
        default='172.31.89.47',
        help='IP of node_1 (default: 172.31.89.47)')
    argparser.add_argument(
        '--node2',
        metavar='H',
        default='127.0.0.1',
        help='IP of node_2 - MASTER (default: 127.0.0.1)')
    argparser.add_argument(
        '--node3',
        metavar='H',
        default='172.31.90.149',
        help='IP of node_3 (default: 172.31.90.149)')
    argparser.add_argument(
        '-p', '--port',
        metavar='P',
        default=2000,
        type=int,
        help='TCP port (default: 2000)')
    argparser.add_argument(
        '--spawn-index',
        metavar='IDX',
        default=0,
        type=int,
        help='Spawn point index to use (default: 0)')
    
    args = argparser.parse_args()
    
    # Define all nodes
    nodes = [
        ('node_1', args.node1, args.port),
        ('node_2', args.node2, args.port),
        ('node_3', args.node3, args.port),
    ]
    
    print('='*70)
    print('Spawning hero vehicle on ALL DisplayCluster nodes')
    print('='*70)
    
    spawned_vehicles = {}
    
    # Spawn on all nodes - order doesn't matter much, but try master first
    for node_name, host, port in nodes:
        vehicle = spawn_vehicle_on_node(host, port, args.spawn_index)
        spawned_vehicles[node_name] = vehicle
        time.sleep(0.5)  # Small delay between spawns
    
    # Summary
    print('\n' + '='*70)
    print('SUMMARY')
    print('='*70)
    
    success_count = sum(1 for v in spawned_vehicles.values() if v)
    failed_count = sum(1 for v in spawned_vehicles.values() if v is False)
    
    for node_name, vehicle in spawned_vehicles.items():
        if vehicle:
            print(f'✓ {node_name}: Vehicle spawned successfully')
        elif vehicle is False:
            print(f'✗ {node_name}: Failed to spawn')
        else:
            print(f'? {node_name}: Connection issue')
    
    print(f'\nTotal: {success_count} succeeded, {failed_count} failed')
    
    if success_count == 3:
        print('\n✓ All nodes have hero vehicle! HeroFollowerActor should now find it.')
        print('The camera should follow the vehicle on all displays.')
        print('\nNow run manual_control.py on the master node to drive the vehicle.')
    else:
        print('\n✗ Some nodes failed. Check connectivity and try again.')

if __name__ == '__main__':
    main()
