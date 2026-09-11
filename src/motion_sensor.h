#pragma once
#include <Arduino.h>

// Fake "motion sensor" tab - a fully self-contained simulation (no real BLE
// or any other sensor involved), styled after Fallout's ghoul/soldier motion
// tracker. A GHOUL and SOLDIER population wanders the semicircle and
// occasionally collides, with a MEDIC and AMMO crate placed once as fixed
// landmarks. See motion_sensor.cpp for the simulation rules.
void MotionSensor_Init();     // call once at boot: seeds the initial population
void MotionSensor_OnEnter();  // call when the active tab becomes this one (resets the movement clock so time away doesn't count as elapsed simulation time)
void MotionSensor_Draw();     // call every loop while this tab is active: advances the simulation one tick and renders it
