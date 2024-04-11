cmake_minimum_required(VERSION 3.16.3...3.28.1)
project(firo_gazebo_plugins)

# Find catkin macros and libraries
find_package(catkin REQUIRED COMPONENTS gazebo_ros roscpp sensor_msgs)
# Find gazebo
find_package(gazebo REQUIRED)
include_directories(
  include
  SYSTEM
  ${catkin_INCLUDE_DIRS}
  ${GAZEBO_INCLUDE_DIRS})

# Catkin specific configuration
catkin_package(
  CATKIN_DEPENDS
  gazebo_ros
  roscpp
  sensor_msgs
  INCLUDE_DIRS
  include
  LIBRARIES
  firo_gazebo_ros_imu_sensor)

# Build
add_library(firo_gazebo_ros_imu_sensor src/firo_gazebo_ros_imu_sensor.cpp)
target_link_libraries(firo_gazebo_ros_imu_sensor ${GAZEBO_LIBRARIES} ${catkin_LIBRARIES})

# Install
install(
  DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.hpp")

install(
  TARGETS firo_gazebo_ros_imu_sensor
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
