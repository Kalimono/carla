#!/usr/bin/env python

# Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma de
# Barcelona (UAB).
#
# This work is licensed under the terms of the MIT license.
# For a copy, see <https://opensource.org/licenses/MIT>.

"""
CARLA Manual Control (No Rendering)

This script spawns and controls a vehicle using keyboard input, but does NOT
render anything in Python. All rendering happens in the editor.

Use ARROWS or WASD keys for control:
    W            : throttle
    S            : brake
    A/D          : steer left/right
    Q            : toggle reverse
    Space        : hand-brake
    P            : toggle autopilot
    M            : toggle manual transmission
    ,/.          : gear up/down
    
    L            : toggle next light type
    SHIFT + L    : toggle high beam
    Z/X          : toggle right/left blinker
    I            : toggle interior light
    
    C            : change weather (Shift+C reverse)
    Backspace    : change vehicle
    
    ESC          : quit
"""

from __future__ import print_function

import glob
import os
import sys
import argparse
import logging
import time

try:
    sys.path.append(glob.glob('../carla/dist/carla-*%d.%d-%s.egg' % (
        sys.version_info.major,
        sys.version_info.minor,
        'win-amd64' if os.name == 'nt' else 'linux-x86_64'))[0])
except IndexError:
    pass

import carla
from carla import ColorConverter as cc

import random

try:
    import pygame
    from pygame.locals import KMOD_CTRL
    from pygame.locals import KMOD_SHIFT
    from pygame.locals import K_BACKSPACE
    from pygame.locals import K_COMMA
    from pygame.locals import K_DOWN
    from pygame.locals import K_ESCAPE
    from pygame.locals import K_LEFT
    from pygame.locals import K_PERIOD
    from pygame.locals import K_RIGHT
    from pygame.locals import K_SLASH
    from pygame.locals import K_SPACE
    from pygame.locals import K_UP
    from pygame.locals import K_a
    from pygame.locals import K_c
    from pygame.locals import K_d
    from pygame.locals import K_i
    from pygame.locals import K_l
    from pygame.locals import K_m
    from pygame.locals import K_p
    from pygame.locals import K_q
    from pygame.locals import K_s
    from pygame.locals import K_w
    from pygame.locals import K_x
    from pygame.locals import K_z
except ImportError:
    raise RuntimeError('cannot import pygame, make sure pygame package is installed')


# ==============================================================================
# -- Global functions ----------------------------------------------------------
# ==============================================================================

def find_weather_presets():
    import re
    rgx = re.compile('.+?(?:(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])|$)')
    name = lambda x: ' '.join(m.group(0) for m in rgx.finditer(x))
    presets = [x for x in dir(carla.WeatherParameters) if re.match('[A-Z].+', x)]
    return [(getattr(carla.WeatherParameters, x), name(x)) for x in presets]


def get_actor_display_name(actor, truncate=250):
    name = ' '.join(actor.type_id.replace('_', '.').title().split('.')[1:])
    return (name[:truncate - 1] + u'\u2026') if len(name) > truncate else name


def get_actor_blueprints(world, filter_str, generation):
    bps = world.get_blueprint_library().filter(filter_str)
    
    if generation.lower() == "all":
        return bps
    
    if len(bps) == 1:
        return bps
    
    try:
        int_generation = int(generation)
        if int_generation in [1, 2, 3]:
            bps = [x for x in bps if int(x.get_attribute('generation')) == int_generation]
            return bps
        else:
            print("   Warning! Actor Generation is not valid. No actor will be spawned.")
            return []
    except:
        print("   Warning! Actor Generation is not valid. No actor will be spawned.")
        return []


# ==============================================================================
# -- World (Simplified - No Rendering) -----------------------------------------
# ==============================================================================

class World(object):
    def __init__(self, carla_world, args):
        self.world = carla_world
        self.sync = args.sync
        self.actor_role_name = args.rolename
        try:
            self.map = self.world.get_map()
        except RuntimeError as error:
            print('RuntimeError: {}'.format(error))
            print('  The server could not send the OpenDRIVE (.xodr) file:')
            print('  Make sure it exists, has the same name of your town, and is correct.')
            sys.exit(1)
        
        self.player = None
        self._actor_filter = args.filter
        self._actor_generation = args.generation
        self._weather_presets = find_weather_presets()
        self._weather_index = 0
        self.restart()

    def restart(self):
        # Get a random blueprint
        blueprint_list = get_actor_blueprints(self.world, self._actor_filter, self._actor_generation)
        if not blueprint_list:
            raise ValueError("Couldn't find any vehicles with filter '{}' generation '{}'".format(
                self._actor_filter, self._actor_generation))
        
        blueprint = random.choice(blueprint_list)
        blueprint.set_attribute('role_name', self.actor_role_name)
        
        if blueprint.has_attribute('color'):
            color = random.choice(blueprint.get_attribute('color').recommended_values)
            blueprint.set_attribute('color', color)
        
        if blueprint.has_attribute('driver_id'):
            driver_id = random.choice(blueprint.get_attribute('driver_id').recommended_values)
            blueprint.set_attribute('driver_id', driver_id)
        
        if blueprint.has_attribute('is_invincible'):
            blueprint.set_attribute('is_invincible', 'true')
        
        # Spawn the player
        if self.player is not None:
            spawn_point = self.player.get_transform()
            spawn_point.location.z += 2.0
            spawn_point.rotation.roll = 0.0
            spawn_point.rotation.pitch = 0.0
            self.destroy()
            self.player = self.world.try_spawn_actor(blueprint, spawn_point)
        
        while self.player is None:
            if not self.map.get_spawn_points():
                print('There are no spawn points available in your map/town.')
                print('Please add some Vehicle Spawn Point to your UE4 scene.')
                sys.exit(1)
            spawn_points = self.map.get_spawn_points()
            spawn_point = random.choice(spawn_points) if spawn_points else carla.Transform()
            self.player = self.world.try_spawn_actor(blueprint, spawn_point)
        
        actor_type = get_actor_display_name(self.player)
        print('Spawned vehicle: {}'.format(actor_type))
        
        if self.sync:
            self.world.tick()
        else:
            self.world.wait_for_tick()

    def next_weather(self, reverse=False):
        self._weather_index += -1 if reverse else 1
        self._weather_index %= len(self._weather_presets)
        preset = self._weather_presets[self._weather_index]
        print('Weather: %s' % preset[1])
        self.world.set_weather(preset[0])

    def tick(self, clock):
        pass

    def destroy(self):
        actors = [self.player]
        for actor in actors:
            if actor is not None:
                actor.destroy()


# ==============================================================================
# -- KeyboardControl (Simplified) ----------------------------------------------
# ==============================================================================

class KeyboardControl(object):
    """Class that handles keyboard input without rendering."""
    def __init__(self, world, start_in_autopilot):
        self._autopilot_enabled = start_in_autopilot
        self._control = carla.VehicleControl()
        self._steer_cache = 0.0
        
        if isinstance(world.player, carla.Vehicle):
            world.player.set_autopilot(self._autopilot_enabled)
        
        print("Controls:")
        print("  WASD/Arrows : Control vehicle")
        print("  Space       : Hand-brake")
        print("  P           : Toggle autopilot")
        print("  M           : Toggle manual transmission")
        print("  , / .       : Gear down/up")
        print("  L           : Toggle lights")
        print("  Z / X       : Toggle blinkers")
        print("  I           : Toggle interior light")
        print("  C           : Change weather")
        print("  Backspace   : Change vehicle")
        print("  ESC         : Quit")

    def parse_events(self, client, world, clock):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return True
            elif event.type == pygame.KEYUP:
                if self._is_quit_shortcut(event.key):
                    return True
                elif event.key == K_BACKSPACE:
                    world.restart()
                elif event.key == K_p:
                    self._autopilot_enabled = not self._autopilot_enabled
                    world.player.set_autopilot(self._autopilot_enabled)
                    print('Autopilot: {}'.format('ON' if self._autopilot_enabled else 'OFF'))
                elif event.key == K_l and pygame.key.get_mods() & KMOD_SHIFT:
                    current_lights = world.player.get_light_state()
                    current_lights ^= carla.VehicleLightState.HighBeam
                    world.player.set_light_state(carla.VehicleLightState(current_lights))
                elif event.key == K_l:
                    current_lights = world.player.get_light_state()
                    current_lights ^= carla.VehicleLightState.Position
                    current_lights ^= carla.VehicleLightState.LowBeam
                    world.player.set_light_state(carla.VehicleLightState(current_lights))
                elif event.key == K_i:
                    current_lights = world.player.get_light_state()
                    current_lights ^= carla.VehicleLightState.Interior
                    world.player.set_light_state(carla.VehicleLightState(current_lights))
                elif event.key == K_z:
                    current_lights = world.player.get_light_state()
                    current_lights ^= carla.VehicleLightState.LeftBlinker
                    world.player.set_light_state(carla.VehicleLightState(current_lights))
                elif event.key == K_x:
                    current_lights = world.player.get_light_state()
                    current_lights ^= carla.VehicleLightState.RightBlinker
                    world.player.set_light_state(carla.VehicleLightState(current_lights))
                elif event.key == K_m:
                    self._control.manual_gear_shift = not self._control.manual_gear_shift
                    self._control.gear = 1 if self._control.manual_gear_shift else 0
                    print('Manual transmission: {}'.format('ON' if self._control.manual_gear_shift else 'OFF'))
                elif self._control.manual_gear_shift and event.key == K_COMMA:
                    self._control.gear = max(-1, self._control.gear - 1)
                    print('Gear: {}'.format(self._control.gear))
                elif self._control.manual_gear_shift and event.key == K_PERIOD:
                    self._control.gear = self._control.gear + 1
                    print('Gear: {}'.format(self._control.gear))
                elif event.key == K_q:
                    self._control.reverse = not self._control.reverse
                    print('Reverse: {}'.format('ON' if self._control.reverse else 'OFF'))
                elif event.key == K_c and pygame.key.get_mods() & KMOD_SHIFT:
                    world.next_weather(reverse=True)
                elif event.key == K_c:
                    world.next_weather()
        
        if not self._autopilot_enabled:
            self._parse_vehicle_keys(pygame.key.get_pressed(), clock.get_time())
            world.player.apply_control(self._control)
        
        return False

    def _parse_vehicle_keys(self, keys, milliseconds):
        self._control.throttle = 1.0 if keys[K_UP] or keys[K_w] else 0.0
        self._control.brake = 1.0 if keys[K_DOWN] or keys[K_s] else 0.0
        
        steer_increment = 5e-4 * milliseconds
        if keys[K_LEFT] or keys[K_a]:
            self._steer_cache -= steer_increment
        elif keys[K_RIGHT] or keys[K_d]:
            self._steer_cache += steer_increment
        else:
            self._steer_cache = 0.0
        
        self._steer_cache = min(0.7, max(-0.7, self._steer_cache))
        self._control.steer = round(self._steer_cache, 1)
        self._control.hand_brake = keys[K_SPACE]

    @staticmethod
    def _is_quit_shortcut(key):
        return key == K_ESCAPE or (key == K_q and pygame.key.get_mods() & KMOD_CTRL)


# ==============================================================================
# -- game_loop() ---------------------------------------------------------------
# ==============================================================================

def game_loop(args):
    pygame.init()
    pygame.font.init()
    
    # Create a minimal window for input handling only
    display = pygame.display.set_mode((400, 100), pygame.HWSURFACE | pygame.DOUBLEBUF)
    pygame.display.set_caption('CARLA Manual Control (Input Only)')
    
    world = None
    
    try:
        client = carla.Client(args.host, args.port)
        client.set_timeout(10.0)
        
        print('Connected to CARLA server at {}:{}'.format(args.host, args.port))
        print('Rendering will appear in Unreal Editor window')
        print('=' * 60)
        
        world_obj = client.get_world()
        
        if args.sync:
            settings = world_obj.get_settings()
            if not settings.synchronous_mode:
                settings.synchronous_mode = True
                settings.fixed_delta_seconds = 0.05
                world_obj.apply_settings(settings)
        
        world = World(world_obj, args)
        controller = KeyboardControl(world, args.autopilot)
        
        clock = pygame.time.Clock()
        
        print('=' * 60)
        print('Press ESC to quit')
        print('=' * 60)
        
        while True:
            if args.sync:
                world_obj.tick()
            
            clock.tick_busy_loop(60)
            
            if controller.parse_events(client, world, clock):
                return
            
            world.tick(clock)
            
            # Clear display (not used for rendering)
            display.fill((0, 0, 0))
            
            # Draw simple info text
            font = pygame.font.Font(pygame.font.get_default_font(), 20)
            text = font.render('Rendering in Editor - Press ESC to quit', True, (255, 255, 255))
            display.blit(text, (10, 40))
            
            pygame.display.flip()
    
    finally:
        if world is not None:
            if args.sync:
                settings = world_obj.get_settings()
                settings.synchronous_mode = False
                world_obj.apply_settings(settings)
            
            world.destroy()
        
        pygame.quit()
        print('Done.')


# ==============================================================================
# -- main() --------------------------------------------------------------------
# ==============================================================================

def main():
    argparser = argparse.ArgumentParser(
        description='CARLA Manual Control (No Rendering)')
    argparser.add_argument(
        '-v', '--verbose',
        action='store_true',
        dest='debug',
        help='print debug information')
    argparser.add_argument(
        '--host',
        metavar='H',
        default='127.0.0.1',
        help='IP of the host server (default: 127.0.0.1)')
    argparser.add_argument(
        '-p', '--port',
        metavar='P',
        default=2000,
        type=int,
        help='TCP port to listen to (default: 2000)')
    argparser.add_argument(
        '-a', '--autopilot',
        action='store_true',
        help='enable autopilot')
    argparser.add_argument(
        '--filter',
        metavar='PATTERN',
        default='vehicle.*',
        help='actor filter (default: "vehicle.*")')
    argparser.add_argument(
        '--generation',
        metavar='G',
        default='2',
        help='restrict to certain actor generation (values: "1","2","All" - default: "2")')
    argparser.add_argument(
        '--rolename',
        metavar='NAME',
        default='hero',
        help='actor role name (default: "hero")')
    argparser.add_argument(
        '--sync',
        action='store_true',
        help='Activate synchronous mode execution')
    
    args = argparser.parse_args()
    
    log_level = logging.DEBUG if args.debug else logging.INFO
    logging.basicConfig(format='%(levelname)s: %(message)s', level=log_level)
    
    logging.info('listening to server %s:%s', args.host, args.port)
    
    print(__doc__)
    
    try:
        game_loop(args)
    except KeyboardInterrupt:
        print('\nCancelled by user. Bye!')


if __name__ == '__main__':
    main()
