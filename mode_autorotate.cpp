/* -----------------------------------------------------------------------------------------
    This is currently a SITL only function until the project is complete.
    To trial this in SITL you will need to use Real Flight 8.
    Instructions for how to set this up in SITL can be found here:
    https://discuss.ardupilot.org/t/autonomous-autorotation-gsoc-project-blog/42139
 -----------------------------------------------------------------------------------------*/

#include "Copter.h"
#include <AC_Autorotation/AC_Autorotation.h>
#include "mode.h"

#include <utility>

#if MODE_AUTOROTATE_ENABLED == ENABLED

#define AUTOROTATE_ENTRY_TIME          2.0f    // (s) number of seconds that the entry phase operates for
#define BAILOUT_MOTOR_RAMP_TIME        1.0f    // (s) time set on bailout ramp up timer for motors - See AC_MotorsHeli_Single
#define HEAD_SPEED_TARGET_RATIO        1.0f    // Normalised target main rotor head speed (unit: -)

bool ModeAutorotate::init(bool ignore_checks)
{
#if FRAME_CONFIG != HELI_FRAME
    // Only allow trad heli to use autorotation mode
    return false;
#endif

    // Check that mode is enabled
    if (!g2.arot.is_enable()) {
        gcs().send_text(MAV_SEVERITY_INFO, "Autorot Mode Not Enabled");
        return false;
    }

    // Check that interlock is disengaged
    if (motors->get_interlock()) {
        gcs().send_text(MAV_SEVERITY_INFO, "Autorot Mode Change Fail: Interlock Engaged");
        return false;
    }

    // Initialise controllers
    // This must be done before RPM value is fetched
    g2.arot.init_hs_controller();
    g2.arot.init_fwd_spd_controller();

    // Retrieve rpm and start rpm sensor health checks
    _initial_rpm = g2.arot.get_rpm(true);

    // Display message 
    gcs().send_text(MAV_SEVERITY_INFO, "Autorotation initiated");

     // Set all inial flags to on
    _flags.entry_initial = 1;
    _flags.ss_glide_initial = 1;
    _flags.flare_initial = 1;
    _flags.touch_down_initial = 1;
    _flags.level_initial = 1;
    _flags.break_initial = 1;
    _flags.straight_ahead_initial = 1;
    _flags.bail_out_initial = 1;
    _msg_flags.bad_rpm = true;

    // Setting default starting switches
    phase_switch = Autorotation_Phase::ENTRY;

    // Set entry timer
    _entry_time_start_ms = millis();

    // The decay rate to reduce the head speed from the current to the target
    _hs_decay = ((_initial_rpm/g2.arot.get_hs_set_point()) - HEAD_SPEED_TARGET_RATIO) / AUTOROTATE_ENTRY_TIME;

    return true;
}



void ModeAutorotate::run()
{
    // Check if interlock becomes engaged
    if (motors->get_interlock() && !copter.ap.land_complete) {
        phase_switch = Autorotation_Phase::BAIL_OUT;
    } else if (motors->get_interlock() && copter.ap.land_complete) {
        // Aircraft is landed and no need to bail out
        set_mode(copter.prev_control_mode, ModeReason::AUTOROTATION_BAILOUT);
    }

    // Current time
    uint32_t now = millis(); //milliseconds

    // Initialise internal variables
    float vz_down = g2.arot.get_vz_down();
    float hgt = g2.arot.get_hgt();
    float rpm = g2.arot.get_rpm();
    float curr_vel_z = inertial_nav.get_velocity_z_up_cms();   // Current vertical descent

    //----------------------------------------------------------------
    //                  State machine logic
    //----------------------------------------------------------------

     // Setting default phase switch positions
     nav_pos_switch = Navigation_Decision::USER_CONTROL_STABILISED;

    // Timer from entry phase to progress to glide phase
    if (phase_switch == Autorotation_Phase::ENTRY){

        if ((now - _entry_time_start_ms)/1000.0f > AUTOROTATE_ENTRY_TIME) {
            // Flight phase can be progressed to steady state glide
            phase_switch = Autorotation_Phase::SS_GLIDE;
        }

    }

        
    //----------------------------------------------------------------
    //                  State machine actions
    //----------------------------------------------------------------
   // Vector2f vel_xy = inertial_nav.get_velocity_xy_cms(); 
    Vector2f pos_home = inertial_nav.get_position_xy_cm();
    //float altitude = inertial_nav.get_position_z_up_cm();
   // float vel_down = inertial_nav.get_velocity_z_up_cms();
   // float h_speed = norm(vel_xy.x,vel_xy.y);
   // float home_dist= norm(pos_home.x,pos_home.y);
    
   // float flight_time = abs(altitude / vel_down);
   // float flight_dist = flight_time * h_speed;

    switch (phase_switch) {

        case Autorotation_Phase::ENTRY:
        {
            
            // Entry phase functions to be run only once
            if (_flags.entry_initial == 1) {
                _flags.entry_initial = 0;
//                #if CONFIG_HAL_BOARD == HAL_BOARD_SITL
                    gcs().send_text(MAV_SEVERITY_INFO, "Entry Phase");
//                #endif
               
               g2.arot.set_collective_value(0.3f);
                _pitch_target = -500.0f;

            }
            
              
            break;
        }

        case Autorotation_Phase::SS_GLIDE:
        {

            if (_flags.ss_glide_initial == 1) {

//                #if CONFIG_HAL_BOARD == HAL_BOARD_SITL
                    gcs().send_text(MAV_SEVERITY_INFO, "SS Glide Phase %f",hgt);
 //               #endif
                _flags.ss_glide_initial = 0;
                g2.arot.set_collective_value(0.4f);
             //   _pitch_target = -300.0f;
            }
            
            g2.arot.set_yaw_cnt_degree(get_bearing_cd(pos_home,Vector2f(0.0f,0.0f)));
             
           if(hgt < 5.0f ){
                phase_switch = Autorotation_Phase::FLARE;
           }
            break;
        }

        case Autorotation_Phase::FLARE:{
            if (_flags.flare_initial == 1) {
                 //   #if CONFIG_HAL_BOARD == HAL_BOARD_SITL
                    gcs().send_text(MAV_SEVERITY_INFO, "FLARE %f %f",vz_down,rpm);
               // #endif
               _flags.flare_initial=0;
               _pitch_target = 3000.0f;
            }
            
            if(hgt < 1.0f ){
                _pitch_target = 0.0f;
                phase_switch = Autorotation_Phase::TOUCH_DOWN;
           }
            break;
            
        }
        case Autorotation_Phase::TOUCH_DOWN:
        {
            if (_flags.touch_down_initial == 1) {
                 //   #if CONFIG_HAL_BOARD == HAL_BOARD_SITL
                    gcs().send_text(MAV_SEVERITY_INFO, "TOUCH_DOWN");
               // #endif
               _flags.touch_down_initial=0;
               

               g2.arot.set_collective_value(1.0f);
            }

            
            break;
        }

        case Autorotation_Phase::BAIL_OUT:
        {
        if (_flags.bail_out_initial == 1) {
                // Functions and settings to be done once are done here.

                #if CONFIG_HAL_BOARD == HAL_BOARD_SITL
                    gcs().send_text(MAV_SEVERITY_INFO, "Bailing Out of Autorotation");
                #endif

                // Set bail out timer remaining equal to the parameter value, bailout time 
                // cannot be less than the motor spool-up time: BAILOUT_MOTOR_RAMP_TIME.
                _bail_time = MAX(g2.arot.get_bail_time(),BAILOUT_MOTOR_RAMP_TIME+0.1f);

                // Set bail out start time
                _bail_time_start_ms = now;

                // Set initial target vertical speed
                _desired_v_z = curr_vel_z;

                // Initialise position and desired velocity
                if (!pos_control->is_active_z()) {
                    pos_control->relax_z_controller(g2.arot.get_last_collective());
                }

                // Get pilot parameter limits
                const float pilot_spd_dn = -get_pilot_speed_dn();
                const float pilot_spd_up = g.pilot_speed_up;

                float pilot_des_v_z = get_pilot_desired_climb_rate(channel_throttle->get_control_in());
                pilot_des_v_z = constrain_float(pilot_des_v_z, pilot_spd_dn, pilot_spd_up);

                // Calculate target climb rate adjustment to transition from bail out descent speed to requested climb rate on stick.
                _target_climb_rate_adjust = (curr_vel_z - pilot_des_v_z)/(_bail_time - BAILOUT_MOTOR_RAMP_TIME); //accounting for 0.5s motor spool time

                // Calculate pitch target adjustment rate to return to level
                _target_pitch_adjust = _pitch_target/_bail_time;

                // set vertical speed and acceleration limits
                pos_control->set_max_speed_accel_z(curr_vel_z, pilot_spd_up, fabsf(_target_climb_rate_adjust));
                pos_control->set_correction_speed_accel_z(curr_vel_z, pilot_spd_up, fabsf(_target_climb_rate_adjust));

                motors->set_desired_spool_state(AP_Motors::DesiredSpoolState::THROTTLE_UNLIMITED);

                _flags.bail_out_initial = 0;
            }

        if ((now - _bail_time_start_ms)/1000.0f >= BAILOUT_MOTOR_RAMP_TIME) {
            // Update desired vertical speed and pitch target after the bailout motor ramp timer has completed
            _desired_v_z -= _target_climb_rate_adjust*G_Dt;
            _pitch_target -= _target_pitch_adjust*G_Dt;
        }
        // Set position controller
        pos_control->set_pos_target_z_from_climb_rate_cm(_desired_v_z);

        // Update controllers
        pos_control->update_z_controller();

        if ((now - _bail_time_start_ms)/1000.0f >= _bail_time) {
            // Bail out timer complete.  Change flight mode. Do not revert back to auto. Prevent aircraft
            // from continuing mission and potentially flying further away after a power failure.
            if (copter.prev_control_mode == Mode::Number::AUTO) {
                set_mode(Mode::Number::ALT_HOLD, ModeReason::AUTOROTATION_BAILOUT);
            } else {
                set_mode(copter.prev_control_mode, ModeReason::AUTOROTATION_BAILOUT);
            }
        }

        break;
        }
    }


    switch (nav_pos_switch) {

        case Navigation_Decision::USER_CONTROL_STABILISED:
        {
            // Operator is in control of roll and yaw.  Controls act as if in stabilise flight mode.  Pitch 
            // is controlled by speed-height controller.
          //  float pilot_roll, pilot_pitch;
           // get_pilot_desired_lean_angles(pilot_roll, pilot_pitch, copter.aparm.angle_max, copter.aparm.angle_max);

            // Get pilot's desired yaw rate
          //  float pilot_yaw_rate = get_pilot_desired_yaw_rate(channel_yaw->norm_input_dz());

            // Pitch target is calculated in autorotation phase switch above
            //attitude_control->input_euler_angle_roll_pitch_euler_rate_yaw(pilot_roll, _pitch_target, pilot_yaw_rate);
           attitude_control->input_euler_angle_roll_pitch_yaw(0.0f, _pitch_target,g2.arot.get_yaw_cnt_degree() ,true);
            break;
        }

        case Navigation_Decision::STRAIGHT_AHEAD:
        case Navigation_Decision::INTO_WIND:
        case Navigation_Decision::NEAREST_RALLY:
        {
            break;
        }
    }

    // Output warning messaged if rpm signal is bad
    if (_flags.bad_rpm) {
        warning_message(1);
    }

} // End function run()

void ModeAutorotate::warning_message(uint8_t message_n)
{
    switch (message_n) {
        case 1:
        {
            if (_msg_flags.bad_rpm) {
                // Bad rpm sensor health.
                gcs().send_text(MAV_SEVERITY_INFO, "Warning: Poor RPM Sensor Health");
                gcs().send_text(MAV_SEVERITY_INFO, "Action: Minimum Collective Applied");
                _msg_flags.bad_rpm = false;
            }
            break;
        }
    }
}

#endif