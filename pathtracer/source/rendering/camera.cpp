#include "camera.h"
#include "math/math_utils.h"
#include <cmath>
#include <string>

Mat4 Camera::lookAt(const Camera& camera) {
    Vec3 center = camera.position + camera.front;
    Vec3 f = normalize(center - camera.position);
    Vec3 s = normalize(cross(f, camera.worldUp));
    Vec3 u = cross(s, f);

    Mat4 mat = Mat4::identity();
    mat.m[0][0] = s.x;
    mat.m[1][0] = s.y;
    mat.m[2][0] = s.z;
    mat.m[0][1] = u.x;
    mat.m[1][1] = u.y;
    mat.m[2][1] = u.z;
    mat.m[0][2] = -f.x;
    mat.m[1][2] = -f.y;
    mat.m[2][2] = -f.z;
    mat.m[3][0] = -dot(s, camera.position);
    mat.m[3][1] = -dot(u, camera.position);
    mat.m[3][2] = dot(f, camera.position);
    return mat;
}

void Camera::updateCameraVectors() {
    Vec3 newFront;
    newFront.x = cos(radians(yaw)) * cos(radians(pitch));
    newFront.y = sin(radians(pitch));
    newFront.z = sin(radians(yaw)) * cos(radians(pitch));
    front = normalize(newFront);
    right = normalize(cross(worldUp, front));
    up = normalize(cross(front, right));
}

void Camera::processKeyboard(const std::string& direction, float deltaTime) {
    float velocity = speed * deltaTime;
    if (direction == "FORWARD")  position += front * velocity;
    if (direction == "BACKWARD") position -= front * velocity;
    if (direction == "LEFT")     position -= right * velocity;
    if (direction == "RIGHT")    position += right * velocity;
    if (direction == "UP")       position += worldUp * velocity;
    if (direction == "DOWN")     position -= worldUp * velocity;
}

void Camera::processMouse(float xoffset, float yoffset) {
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // Constrain pitch to avoid gimbal lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    updateCameraVectors();
}