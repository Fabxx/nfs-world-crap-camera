#define NOMINMAX
#include "CameraLogicUpdateHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>
#include "../Data/Globals.h"

namespace NFS {
    namespace Hooks {

        static Util::Logger& L = Util::Logger::Instance();

        using BuildViewMatrixFunc_t = void(__cdecl*)(void* out_matrix, float* camera_pos, float* target_pos, void* up_vector);
        static BuildViewMatrixFunc_t g_originalBuildViewMatrix = nullptr;

        using CameraLogic_Update_t = void(__thiscall*)(void* cameraState, float deltaTime);
        static CameraLogic_Update_t g_original_CameraLogic_Update = nullptr;

        // Collision detection function signature (FUN_00e5c360)
        using CollisionCheckFunc_t = bool(__cdecl*)(float* pos1, float* pos2);
        static CollisionCheckFunc_t g_collisionCheck = nullptr;

        static void* g_pCurrentCameraState = nullptr;
        static float g_lastDeltaTime = 0.016f;

        struct vec3 { float x, y, z; };

        namespace Offsets {
            constexpr uintptr_t CameraState_CameraMode = 0xA8;
            constexpr uintptr_t CameraState_TransformComponentPtr = 0x90;
            constexpr uintptr_t TransformComponent_Position = 0x20;
            constexpr uintptr_t TransformComponent_RotationMatrix = 0x30;
            constexpr uintptr_t PhysicsComponent_Speed = 0xA0;
            constexpr uintptr_t PhysicsComponent_Velocity = 0x80;
        }

        inline vec3 vec3_sub(const vec3& a, const vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        inline float vec3_dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        inline vec3 vec3_cross(const vec3& a, const vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        inline vec3 vec3_normalize(const vec3& v) {
            float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
            if (length > 1e-6f) { return { v.x / length, v.y / length, v.z / length }; }
            return { 0.0f, 0.0f, 0.0f };
        }

        inline float damped_lerp(float current, float target, float stiffness, float dt) {
            float factor = 1.0f - expf(-stiffness * dt);
            return current + (target - current) * factor;
        }

        void __cdecl DetouredBuildViewMatrix(void* out_matrix_void, float* camera_pos_arr, float* target_pos_arr, void* up_vector_void) {
            if (!g_pCurrentCameraState) {
                g_originalBuildViewMatrix(out_matrix_void, camera_pos_arr, target_pos_arr, up_vector_void);
                return;
            }

            int cameraMode = *reinterpret_cast<int*>(static_cast<char*>(g_pCurrentCameraState) + Offsets::CameraState_CameraMode);
            static int lastCameraMode = -1;
            static bool needsReset = true;

            if (cameraMode != 2 && cameraMode != 3) {
                needsReset = true;
                lastCameraMode = cameraMode;
                g_originalBuildViewMatrix(out_matrix_void, camera_pos_arr, target_pos_arr, up_vector_void);
                return;
            }

            if (lastCameraMode != cameraMode) {
                needsReset = true;
                lastCameraMode = cameraMode;
            }

            void* pTransformComponent = *reinterpret_cast<void**>(static_cast<char*>(g_pCurrentCameraState) + Offsets::CameraState_TransformComponentPtr);
            if (!pTransformComponent) {
                g_originalBuildViewMatrix(out_matrix_void, camera_pos_arr, target_pos_arr, up_vector_void);
                return;
            }

            constexpr float distanceStiffness = 5.0f;
            constexpr float heightStiffness = 8.0f;

            constexpr float minOrientationStiffness = 0.1f;
            constexpr float maxOrientationStiffness = 0.4f;

            constexpr float baseDistClose = 4.0f, baseDistFar = 5.5f;
            constexpr float speedDistFactor = 2.0f;
            constexpr float baseHeight = 0.5f;
            constexpr float targetHeightOffset = 0.8f;

            char* transformBytes = static_cast<char*>(pTransformComponent);
            vec3 vehiclePos = *reinterpret_cast<vec3*>(transformBytes + Offsets::TransformComponent_Position);
            float* vehicleMatrix = reinterpret_cast<float*>(transformBytes + Offsets::TransformComponent_RotationMatrix);

            vec3 vehicleForward = { vehicleMatrix[0], vehicleMatrix[1], vehicleMatrix[2] };
            vec3 vehicleUp = { vehicleMatrix[8], vehicleMatrix[9], vehicleMatrix[10] };

            float speed = *reinterpret_cast<float*>(transformBytes + Offsets::PhysicsComponent_Speed);
            float speedNormalized = std::min(1.0f, std::max(0.0f, speed / 80.0f));

            float dynamicOrientationStiffness = minOrientationStiffness + (maxOrientationStiffness - minOrientationStiffness) * speedNormalized;

            float idealDistance = ((cameraMode == 2) ? baseDistClose : baseDistFar) + (speedNormalized * speedDistFactor);

            vec3 idealTargetPos = {
                vehiclePos.x + vehicleUp.x * targetHeightOffset,
                vehiclePos.y + vehicleUp.y * targetHeightOffset,
                vehiclePos.z + vehicleUp.z * targetHeightOffset
            };

            static vec3 currentCameraPos;
            static vec3 currentCameraFwd;
            static float currentDistance;

            if (needsReset) {
                currentCameraFwd = vehicleForward;
                currentDistance = idealDistance;
                currentCameraPos = {
                    vehiclePos.x - currentCameraFwd.x * currentDistance,
                    vehiclePos.y - currentCameraFwd.y * currentDistance,
                    vehiclePos.z - currentCameraFwd.z * currentDistance
                };
                needsReset = false;
            }

            float orientationLerpFactor = 1.0f - expf(-dynamicOrientationStiffness * g_lastDeltaTime);
            currentCameraFwd.x += (vehicleForward.x - currentCameraFwd.x) * orientationLerpFactor;
            currentCameraFwd.y += (vehicleForward.y - currentCameraFwd.y) * orientationLerpFactor;
            currentCameraFwd.z += (vehicleForward.z - currentCameraFwd.z) * orientationLerpFactor;
            currentCameraFwd = vec3_normalize(currentCameraFwd);

            currentDistance = damped_lerp(currentDistance, idealDistance, distanceStiffness, g_lastDeltaTime);

            vec3 finalCameraPos;
            finalCameraPos.x = vehiclePos.x - currentCameraFwd.x * currentDistance;
            finalCameraPos.y = vehiclePos.y - currentCameraFwd.y * currentDistance;
            finalCameraPos.z = (vehiclePos.z - currentCameraFwd.z * currentDistance) + targetHeightOffset;

            // Collision detection system
            /*if (g_collisionCheck) {
                float testPos[3] = { finalCameraPos.x, finalCameraPos.y, finalCameraPos.z };
                float vehiclePosForCheck[3] = { vehiclePos.x, vehiclePos.y, vehiclePos.z };

                bool collision = g_collisionCheck(testPos, vehiclePosForCheck);

                if (collision) {
                    // Strategy 1: Try moving camera up
                    testPos[0] = finalCameraPos.x;
                    testPos[1] = finalCameraPos.y;
                    testPos[2] = finalCameraPos.z + 2.0f; // Move up by 2 units

                    collision = g_collisionCheck(testPos, vehiclePosForCheck);

                    if (!collision) {
                        // Success - use elevated position
                        finalCameraPos.z = testPos[2];
                    }
                    else {
                        // Strategy 2: Move closer to vehicle (50% distance)
                        float adjustedDistance = currentDistance * 0.5f;
                        testPos[0] = vehiclePos.x - currentCameraFwd.x * adjustedDistance;
                        testPos[1] = vehiclePos.y - currentCameraFwd.y * adjustedDistance;
                        testPos[2] = (vehiclePos.z - currentCameraFwd.z * adjustedDistance) + targetHeightOffset;

                        collision = g_collisionCheck(testPos, vehiclePosForCheck);

                        if (!collision) {
                            // Success - use closer position
                            finalCameraPos.x = testPos[0];
                            finalCameraPos.y = testPos[1];
                            finalCameraPos.z = testPos[2];
                        }
                        else {
                            // Strategy 3: Last resort - very close (25% distance)
                            adjustedDistance = currentDistance * 0.25f;
                            testPos[0] = vehiclePos.x - currentCameraFwd.x * adjustedDistance;
                            testPos[1] = vehiclePos.y - currentCameraFwd.y * adjustedDistance;
                            testPos[2] = (vehiclePos.z - currentCameraFwd.z * adjustedDistance) + targetHeightOffset;

                            collision = g_collisionCheck(testPos, vehiclePosForCheck);

                            if (!collision) {
                                finalCameraPos.x = testPos[0];
                                finalCameraPos.y = testPos[1];
                                finalCameraPos.z = testPos[2];
                            }
                            else {
                                // Even closer didn't work - just stay at closest position
                                // The game will handle extreme cases
                                finalCameraPos.x = testPos[0];
                                finalCameraPos.y = testPos[1];
                                finalCameraPos.z = testPos[2];
                            }
                        }
                    }
                }
            }*/

            currentCameraPos = finalCameraPos;

            vec3 forward = vec3_normalize(vec3_sub(idealTargetPos, finalCameraPos));
            vec3 worldUp = { 0.0f, 0.0f, 1.0f };
            vec3 right = vec3_normalize(vec3_cross(forward, worldUp));
            vec3 new_up = vec3_cross(forward, right);

            float* m = reinterpret_cast<float*>(out_matrix_void);
            m[0] = right.x;   m[1] = new_up.x;  m[2] = forward.x;   m[3] = 0.0f;
            m[4] = right.y;   m[5] = new_up.y;  m[6] = forward.y;   m[7] = 0.0f;
            m[8] = right.z;   m[9] = new_up.z;  m[10] = forward.z;  m[11] = 0.0f;
            m[12] = -vec3_dot(right, finalCameraPos);
            m[13] = -vec3_dot(new_up, finalCameraPos);
            m[14] = -vec3_dot(forward, finalCameraPos);
            m[15] = 1.0f;
        }

        void __fastcall DetouredCameraLogicUpdate(void* cameraState, void* edx, float deltaTime) {
            g_pCurrentCameraState = cameraState;
            g_lastDeltaTime = (deltaTime > 0.0f && deltaTime < 0.1f) ? deltaTime : 0.016f;
            g_original_CameraLogic_Update(cameraState, deltaTime);
        }

        constexpr uintptr_t CAMERA_LOGIC_UPDATE_OFFSET = 0x3DA1E0;
        constexpr uintptr_t BUILD_VIEW_MATRIX_OFFSET = 0x235D50;
        constexpr uintptr_t COLLISION_CHECK_OFFSET = 0x3CC360; // FUN_00e5c360

        bool SetupCameraHooks() {
            if (g_original_CameraLogic_Update) return true;
            L.Get()->info("Setting up dual camera hooks with collision detection...");

            // Setup collision check function
            g_collisionCheck = reinterpret_cast<CollisionCheckFunc_t>(g_moduleBase + COLLISION_CHECK_OFFSET);
            L.Get()->info("Collision check function at: 0x{:X}", (uintptr_t)g_collisionCheck);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            uintptr_t cameraUpdateAddress = g_moduleBase + CAMERA_LOGIC_UPDATE_OFFSET;
            g_original_CameraLogic_Update = reinterpret_cast<CameraLogic_Update_t>(cameraUpdateAddress);
            LONG error = DetourAttach(&(PVOID&)g_original_CameraLogic_Update, (void*)DetouredCameraLogicUpdate);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach for CameraLogic_Update failed: {}", error);
                DetourTransactionAbort();
                return false;
            }

            uintptr_t buildViewMatrixAddress = g_moduleBase + BUILD_VIEW_MATRIX_OFFSET;
            g_originalBuildViewMatrix = reinterpret_cast<BuildViewMatrixFunc_t>(buildViewMatrixAddress);
            error = DetourAttach(&(PVOID&)g_originalBuildViewMatrix, (void*)DetouredBuildViewMatrix);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach for BuildViewMatrix failed: {}", error);
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed: {}", error);
                return false;
            }
            L.Get()->info("Dual camera hooks with collision detection attached successfully.");
            return true;
        }

        bool CleanCameraHooks() {
            if (!g_original_CameraLogic_Update) return true;
            L.Get()->info("Removing dual camera hooks...");
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            if (g_original_CameraLogic_Update) DetourDetach(&(PVOID&)g_original_CameraLogic_Update, (void*)DetouredCameraLogicUpdate);
            if (g_originalBuildViewMatrix) DetourDetach(&(PVOID&)g_originalBuildViewMatrix, (void*)DetouredBuildViewMatrix);
            DetourTransactionCommit();
            g_original_CameraLogic_Update = nullptr;
            g_originalBuildViewMatrix = nullptr;
            g_pCurrentCameraState = nullptr;
            g_collisionCheck = nullptr;
            L.Get()->info("Dual camera hooks removed successfully.");
            return true;
        }
    }
}