/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * DO NOT EDIT MANUALLY!!!
 *
 * Generated by tools/generate_annotation_enums.py.
 */

// clang-format off

package android.hardware.automotive.vehicle;

import java.util.Map;

public final class ChangeModeForVehicleProperty {

    public static final Map<Integer, Integer> values = Map.ofEntries(
        Map.entry(VehicleProperty.INFO_VIN, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_MAKE, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_MODEL, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_MODEL_YEAR, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_FUEL_CAPACITY, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_FUEL_TYPE, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_EV_BATTERY_CAPACITY, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_EV_CONNECTOR_TYPE, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_FUEL_DOOR_LOCATION, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_EV_PORT_LOCATION, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_DRIVER_SEAT, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_EXTERIOR_DIMENSIONS, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_MULTI_EV_PORT_LOCATIONS, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_MODEL_TRIM, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INFO_VEHICLE_SIZE_CLASS, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.PERF_ODOMETER, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.PERF_VEHICLE_SPEED, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.PERF_VEHICLE_SPEED_DISPLAY, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.PERF_STEERING_ANGLE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.PERF_REAR_STEERING_ANGLE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.INSTANTANEOUS_FUEL_ECONOMY, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.INSTANTANEOUS_EV_EFFICIENCY, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.ENGINE_COOLANT_TEMP, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.ENGINE_OIL_LEVEL, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ENGINE_OIL_TEMP, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.ENGINE_RPM, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.WHEEL_TICK, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.FUEL_LEVEL, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.FUEL_DOOR_OPEN, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_BATTERY_LEVEL, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.EV_CURRENT_BATTERY_CAPACITY, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_PORT_OPEN, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_PORT_CONNECTED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_BATTERY_INSTANTANEOUS_CHARGE_RATE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.RANGE_REMAINING, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.EV_BATTERY_AVERAGE_TEMPERATURE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.TIRE_PRESSURE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.CRITICALLY_LOW_TIRE_PRESSURE, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ACCELERATOR_PEDAL_COMPRESSION_PERCENTAGE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.BRAKE_PEDAL_COMPRESSION_PERCENTAGE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.BRAKE_PAD_WEAR_PERCENTAGE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.BRAKE_FLUID_LEVEL_LOW, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_PASSIVE_SUSPENSION_HEIGHT, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.ENGINE_IDLE_AUTO_STOP_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.IMPACT_DETECTED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_HORN_ENGAGED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.GEAR_SELECTION, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CURRENT_GEAR, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.PARKING_BRAKE_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.PARKING_BRAKE_AUTO_APPLY, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_BRAKE_REGENERATION_LEVEL, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FUEL_LEVEL_LOW, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.NIGHT_MODE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.TURN_SIGNAL_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.IGNITION_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ABS_ACTIVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.TRACTION_CONTROL_ACTIVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_STOPPING_MODE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ELECTRONIC_STABILITY_CONTROL_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ELECTRONIC_STABILITY_CONTROL_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.TURN_SIGNAL_LIGHT_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.TURN_SIGNAL_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_FAN_SPEED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_FAN_DIRECTION, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_TEMPERATURE_CURRENT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_TEMPERATURE_SET, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_DEFROSTER, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_AC_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_MAX_AC_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_MAX_DEFROST_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_RECIRC_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_DUAL_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_AUTO_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_SEAT_TEMPERATURE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_SIDE_MIRROR_HEAT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_STEERING_WHEEL_HEAT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_TEMPERATURE_DISPLAY_UNITS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_ACTUAL_FAN_SPEED_RPM, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_POWER_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_FAN_DIRECTION_AVAILABLE, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.HVAC_AUTO_RECIRC_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_SEAT_VENTILATION, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_ELECTRIC_DEFROSTER_ON, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HVAC_TEMPERATURE_VALUE_SUGGESTION, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DISTANCE_DISPLAY_UNITS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FUEL_VOLUME_DISPLAY_UNITS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.TIRE_PRESSURE_DISPLAY_UNITS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_BATTERY_DISPLAY_UNITS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FUEL_CONSUMPTION_UNITS_DISTANCE_OVER_VOLUME, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_SPEED_DISPLAY_UNITS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EXTERNAL_CAR_TIME, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ANDROID_EPOCH_TIME, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STORAGE_ENCRYPTION_BINDING_SEED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ENV_OUTSIDE_TEMPERATURE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.AP_POWER_STATE_REQ, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.AP_POWER_STATE_REPORT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.AP_POWER_BOOTUP_REASON, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.DISPLAY_BRIGHTNESS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.PER_DISPLAY_BRIGHTNESS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VALET_MODE_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HEAD_UP_DISPLAY_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HW_KEY_INPUT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HW_KEY_INPUT_V2, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HW_MOTION_INPUT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HW_ROTARY_INPUT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HW_CUSTOM_INPUT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DOOR_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DOOR_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DOOR_LOCK, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DOOR_CHILD_LOCK_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_Z_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_Z_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_Y_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_Y_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_LOCK, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_FOLD, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_AUTO_FOLD_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.MIRROR_AUTO_TILT_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_MEMORY_SELECT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_MEMORY_SET, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BELT_BUCKLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BELT_HEIGHT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BELT_HEIGHT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_FORE_AFT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_FORE_AFT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BACKREST_ANGLE_1_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BACKREST_ANGLE_1_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BACKREST_ANGLE_2_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BACKREST_ANGLE_2_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEIGHT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEIGHT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_DEPTH_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_DEPTH_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_TILT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_TILT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_LUMBAR_FORE_AFT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_LUMBAR_FORE_AFT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_LUMBAR_SIDE_SUPPORT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_LUMBAR_SIDE_SUPPORT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_HEIGHT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_HEIGHT_POS_V2, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_HEIGHT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_ANGLE_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_ANGLE_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_FORE_AFT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_HEADREST_FORE_AFT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_FOOTWELL_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_FOOTWELL_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_EASY_ACCESS_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_AIRBAG_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_AIRBAGS_DEPLOYED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_CUSHION_SIDE_SUPPORT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_CUSHION_SIDE_SUPPORT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_LUMBAR_VERTICAL_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_LUMBAR_VERTICAL_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_WALK_IN_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_BELT_PRETENSIONER_DEPLOYED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SEAT_OCCUPANCY, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WINDOW_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WINDOW_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WINDOW_LOCK, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WINDSHIELD_WIPERS_PERIOD, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WINDSHIELD_WIPERS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WINDSHIELD_WIPERS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_DEPTH_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_DEPTH_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_HEIGHT_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_HEIGHT_MOVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_THEFT_LOCK_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_LOCKED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_EASY_ACCESS_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.GLOVE_BOX_DOOR_POS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.GLOVE_BOX_LOCKED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_MAP_SERVICE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LOCATION_CHARACTERIZATION, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ULTRASONICS_SENSOR_POSITION, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ULTRASONICS_SENSOR_ORIENTATION, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ULTRASONICS_SENSOR_FIELD_OF_VIEW, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ULTRASONICS_SENSOR_DETECTION_RANGE, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ULTRASONICS_SENSOR_SUPPORTED_RANGES, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.ULTRASONICS_SENSOR_MEASURED_DISTANCE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.OBD2_LIVE_FRAME, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.OBD2_FREEZE_FRAME, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.OBD2_FREEZE_FRAME_INFO, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.OBD2_FREEZE_FRAME_CLEAR, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HEADLIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HIGH_BEAM_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FOG_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HAZARD_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HEADLIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HIGH_BEAM_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FOG_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HAZARD_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CABIN_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CABIN_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.READING_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.READING_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.STEERING_WHEEL_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SUPPORT_CUSTOMIZE_VENDOR_PERMISSION, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.DISABLED_OPTIONAL_FEATURES, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.INITIAL_USER_INFO, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.SWITCH_USER, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CREATE_USER, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.REMOVE_USER, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.USER_IDENTIFICATION_ASSOCIATION, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EVS_SERVICE_REQUEST, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.POWER_POLICY_REQ, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.POWER_POLICY_GROUP_REQ, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CURRENT_POWER_POLICY, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WATCHDOG_ALIVE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.WATCHDOG_TERMINATED_PROCESS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VHAL_HEARTBEAT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CLUSTER_SWITCH_UI, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CLUSTER_DISPLAY_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CLUSTER_REPORT_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CLUSTER_REQUEST_DISPLAY, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CLUSTER_NAVIGATION_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ELECTRONIC_TOLL_COLLECTION_CARD_TYPE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ELECTRONIC_TOLL_COLLECTION_CARD_STATUS, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FRONT_FOG_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FRONT_FOG_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.REAR_FOG_LIGHTS_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.REAR_FOG_LIGHTS_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_CURRENT_DRAW_LIMIT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_PERCENT_LIMIT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_SWITCH, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EV_CHARGE_TIME_REMAINING, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.EV_REGENERATIVE_BRAKING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.TRAILER_PRESENT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_CURB_WEIGHT, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.GENERAL_SAFETY_REGULATION_COMPLIANCE_REQUIREMENT, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.SUPPORTED_PROPERTY_IDS, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.SHUTDOWN_REQUEST, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_IN_USE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CLUSTER_HEARTBEAT, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_DRIVING_AUTOMATION_CURRENT_LEVEL, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.VEHICLE_DRIVING_AUTOMATION_TARGET_LEVEL, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CAMERA_SERVICE_CURRENT_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.PER_DISPLAY_MAX_BRIGHTNESS, VehiclePropertyChangeMode.STATIC),
        Map.entry(VehicleProperty.AUTOMATIC_EMERGENCY_BRAKING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.AUTOMATIC_EMERGENCY_BRAKING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FORWARD_COLLISION_WARNING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.FORWARD_COLLISION_WARNING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.BLIND_SPOT_WARNING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.BLIND_SPOT_WARNING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_DEPARTURE_WARNING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_DEPARTURE_WARNING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_KEEP_ASSIST_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_KEEP_ASSIST_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_CENTERING_ASSIST_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_CENTERING_ASSIST_COMMAND, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LANE_CENTERING_ASSIST_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EMERGENCY_LANE_KEEP_ASSIST_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.EMERGENCY_LANE_KEEP_ASSIST_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CRUISE_CONTROL_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CRUISE_CONTROL_TYPE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CRUISE_CONTROL_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CRUISE_CONTROL_COMMAND, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CRUISE_CONTROL_TARGET_SPEED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ADAPTIVE_CRUISE_CONTROL_TARGET_TIME_GAP, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.ADAPTIVE_CRUISE_CONTROL_LEAD_VEHICLE_MEASURED_DISTANCE, VehiclePropertyChangeMode.CONTINUOUS),
        Map.entry(VehicleProperty.HANDS_ON_DETECTION_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HANDS_ON_DETECTION_DRIVER_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.HANDS_ON_DETECTION_WARNING, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DROWSINESS_ATTENTION_SYSTEM_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DROWSINESS_ATTENTION_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DROWSINESS_ATTENTION_WARNING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DROWSINESS_ATTENTION_WARNING, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DISTRACTION_SYSTEM_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DISTRACTION_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DISTRACTION_WARNING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.DRIVER_DISTRACTION_WARNING, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LOW_SPEED_COLLISION_WARNING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LOW_SPEED_COLLISION_WARNING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CROSS_TRAFFIC_MONITORING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.CROSS_TRAFFIC_MONITORING_WARNING_STATE, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LOW_SPEED_AUTOMATIC_EMERGENCY_BRAKING_ENABLED, VehiclePropertyChangeMode.ON_CHANGE),
        Map.entry(VehicleProperty.LOW_SPEED_AUTOMATIC_EMERGENCY_BRAKING_STATE, VehiclePropertyChangeMode.ON_CHANGE)
    );

}
