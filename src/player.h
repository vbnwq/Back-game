#pragma once
// =============================================================
//  First-person player controller.
//  - WASD movement, mouse look
//  - Left Shift = genuine SPRINT (large speed increase) gated by
//    a stamina system; FOV widening is purely a visual bonus.
//  - Space = jump (gravity + landing impact)
//  - Smooth acceleration / deceleration
//  - Realistic head-bob (vertical + lateral), breathing sway when
//    idle, sprint lean, and landing dip for movement feedback.
//  - Robust AABB grid collision with wall sliding.
// =============================================================
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "map.h"
#include <cmath>
#include <algorithm>

struct Player {
    glm::vec3 pos = glm::vec3(10.0f, 1.7f, 10.0f); // feet/eye base
    glm::vec3 vel = glm::vec3(0.0f);
    float yaw = 0.0f, pitch = 0.0f;
    float baseEye = 1.7f;
    float radius = 0.40f;

    // speeds (m/s) - tuned for more responsive, snappier movement
    float walkSpeed   = 4.5f;
    float sprintSpeed = 8.8f;   // > 2x walk -> obviously faster

    // survivability / progression (raised capacity for better survivability)
    float health      = 125.0f;
    float maxHealth   = 175.0f;   // increased capacity
    int   level       = 1;
    float xp          = 0.0f;
    float xpToNext    = 100.0f;
    // status effects (bitfield-ish flags for the HUD)
    bool  injured     = false;    // low health
    bool  winded      = false;    // exhausted stamina

    // footstep bookkeeping (driven in main via distance accumulator)
    float stepAccum   = 0.0f;
    bool  stepFootR   = false;

    // dynamics / feedback state
    float bobTime  = 0.0f;
    float bobAmount= 0.0f;      // smoothed walk intensity 0..1
    float curSpeed = 0.0f;
    float sprintBlend = 0.0f;   // smoothed sprint state for FOV/lean
    float breathe = 0.0f;

    // stamina 0..1
    float stamina = 1.0f;
    bool  exhausted = false;

    // jump / gravity
    bool  onGround = true;
    float vY = 0.0f;
    float eyeOffset = 0.0f;     // vertical offset from jump
    float landDip = 0.0f;       // transient camera dip on landing
    float prevVY = 0.0f;

    glm::vec3 front() const {
        glm::vec3 f;
        f.x = cosf(glm::radians(yaw))*cosf(glm::radians(pitch));
        f.y = sinf(glm::radians(pitch));
        f.z = sinf(glm::radians(yaw))*cosf(glm::radians(pitch));
        return glm::normalize(f);
    }
    glm::vec3 frontFlat() const {
        return glm::normalize(glm::vec3(cosf(glm::radians(yaw)),0,sinf(glm::radians(yaw))));
    }
    glm::vec3 right() const {
        return glm::normalize(glm::cross(frontFlat(), glm::vec3(0,1,0)));
    }

    void mouse(float dx,float dy,float sens){
        yaw += dx*sens;
        pitch -= dy*sens;
        pitch = std::clamp(pitch, -89.0f, 89.0f);
    }

    void addXP(float amount){
        xp += amount;
        while(xp >= xpToNext){
            xp -= xpToNext;
            level++;
            xpToNext *= 1.35f;
            maxHealth += 10.0f;
            health = std::min(maxHealth, health + 30.0f); // level-up heal
        }
    }

    // axis-separated move so we slide along walls (never stick)
    void tryMove(glm::vec3 delta){
        float nx = pos.x + delta.x;
        if(!bmap::blocked(nx, pos.z, radius)) pos.x = nx;
        float nz = pos.z + delta.z;
        if(!bmap::blocked(pos.x, nz, radius)) pos.z = nz;
    }

    // returns whether the player is actually sprinting this frame
    bool update(float dt, glm::vec3 wishDir, bool sprintKey, bool jump){
        bool moving = glm::length(wishDir) > 0.1f;
        if(moving) wishDir = glm::normalize(wishDir);

        // --- stamina + sprint gating ---
        bool wantSprint = sprintKey && moving && !exhausted && stamina>0.02f;
        if(wantSprint){
            stamina -= dt*0.32f;            // drains in ~3s of full sprint
            if(stamina<=0.0f){ stamina=0.0f; exhausted=true; }
        } else {
            stamina += dt*0.22f;            // recovers
            if(stamina>=1.0f){ stamina=1.0f; }
            if(exhausted && stamina>0.30f) exhausted=false; // need to recover a bit
        }
        bool sprint = wantSprint;

        float targetSpeed = sprint ? sprintSpeed : walkSpeed;
        // crouch-ish slow when exhausted & holding shift
        if(sprintKey && exhausted) targetSpeed = walkSpeed*0.85f;

        // --- smooth horizontal velocity (snappier response) ---
        glm::vec3 desired = moving ? wishDir*targetSpeed : glm::vec3(0);
        float accel = moving ? (sprint?22.0f:18.0f) : 16.0f;
        float a = std::clamp(accel*dt, 0.0f, 1.0f);
        vel.x = glm::mix(vel.x, desired.x, a);
        vel.z = glm::mix(vel.z, desired.z, a);

        tryMove(glm::vec3(vel.x*dt, 0, vel.z*dt));

        // --- jump + gravity ---
        if(jump && onGround){ vY = 4.6f; onGround=false; }
        prevVY = vY;
        vY -= 15.0f*dt;
        eyeOffset += vY*dt;
        // hard cap so the player can never rise to peek over walls
        if(baseEye + eyeOffset > bmap::WALL_H - 0.55f){
            eyeOffset = (bmap::WALL_H - 0.55f) - baseEye;
            if(vY>0) vY = 0;
        }
        if(eyeOffset <= 0.0f){
            if(!onGround && prevVY<-3.0f){          // landed hard
                landDip = std::min(0.18f, -prevVY*0.03f);
            }
            eyeOffset=0.0f; vY=0; onGround=true;
        }
        landDip = glm::mix(landDip, 0.0f, std::clamp(8.0f*dt,0.f,1.f));

        // --- head bob driven by horizontal speed ---
        curSpeed = glm::length(glm::vec2(vel.x,vel.z));
        float intensity = std::clamp(curSpeed / sprintSpeed, 0.0f, 1.0f);
        bobAmount = glm::mix(bobAmount, moving?std::max(0.35f,intensity):0.0f, std::clamp(8.0f*dt,0.f,1.f));
        float bobFreq = sprint? 13.5f : 9.0f;
        if(curSpeed>0.05f) bobTime += dt*bobFreq;

        // sprint blend (for FOV + lean) and idle breathing
        sprintBlend = glm::mix(sprintBlend, sprint?1.0f:0.0f, std::clamp(6.0f*dt,0.f,1.f));
        breathe += dt*1.6f;

        pos.y = baseEye + eyeOffset;

        // --- step distance accumulator for footstep audio ---
        stepAccum += curSpeed*dt;

        // --- status / passive regen ---
        injured = health < maxHealth*0.30f;
        winded  = exhausted;
        // slow health regeneration when not exhausted (slightly faster recovery)
        if(!exhausted && health < maxHealth) health = std::min(maxHealth, health + dt*2.2f);

        return sprint;
    }

    // camera matrix including head bob, breathing, lean, land dip
    glm::mat4 view(glm::vec3& outEye) const {
        float vbob = sinf(bobTime)*0.055f*bobAmount;
        float hbob = cosf(bobTime*0.5f)*0.040f*bobAmount;
        float idleBreath = (1.0f-bobAmount)*sinf(breathe)*0.012f;
        glm::vec3 eye = pos;
        eye.y += vbob + idleBreath - landDip;
        eye += right()*hbob;

        glm::vec3 f = front();
        glm::vec3 up = glm::vec3(0,1,0);
        // subtle camera roll while strafing/sprinting for liveliness
        float roll = -hbob*0.6f - sprintBlend*0.015f;
        glm::vec3 r = right();
        up = glm::normalize(up + r*roll);
        outEye = eye;
        return glm::lookAt(eye, eye+f, up);
    }
};
