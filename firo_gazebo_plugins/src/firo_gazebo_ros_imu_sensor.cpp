/* Copyright [2015] [Alessandro Settimi]
 *
 * email: ale.settimi@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This code is based on the gazebo_ros_imu_sensor plugin from the gazebo_ros_pkgs
 * Original code repository: https://github.com/ros-simulation/gazebo_ros_pkgs/
 *
 * Modified versions of the original code are licensed under the same
 * Apache License, Version 2.0.
 */

#include <firo_gazebo_plugins/firo_gazebo_ros_imu_sensor.h>
#include <gazebo/sensors/ImuSensor.hh>
#include <gazebo/physics/World.hh>
#ifdef ENABLE_PROFILER
#include <ignition/common/Profiler.hh>
#endif
#include <ignition/math/Rand.hh>

GZ_REGISTER_SENSOR_PLUGIN(gazebo::GazeboRosImuSensor)

gazebo::GazeboRosImuSensor::GazeboRosImuSensor() : SensorPlugin()
{
  accelerometer_data = ignition::math::Vector3d(0, 0, 0);
  gyroscope_data = ignition::math::Vector3d(0, 0, 0);
  orientation = ignition::math::Quaterniond(1, 0, 0, 0);
  sensor = NULL;
}

void gazebo::GazeboRosImuSensor::Load(gazebo::sensors::SensorPtr sensor_, sdf::ElementPtr sdf_)
{
  sdf = sdf_;
  sensor = dynamic_cast<gazebo::sensors::ImuSensor*>(sensor_.get());

  if (sensor == NULL)
  {
    ROS_FATAL("Error: Sensor pointer is NULL!");
    return;
  }

  bool initial_orientation_as_reference = false;
  if (!sdf->HasElement("initialOrientationAsReference"))
  {
    ROS_INFO(
        "<initialOrientationAsReference> is unset, using default value of false "
        "to comply with REP 145 (world as orientation reference)");
  }
  else
  {
    initial_orientation_as_reference = sdf->Get<bool>("initialOrientationAsReference");
  }

  if (initial_orientation_as_reference)
  {
    ROS_WARN(
        "<initialOrientationAsReference> set to true, this behavior is deprecated "
        "as it does not comply with REP 145.");
  }
  else
  {
    // This complies with REP 145
    sensor->SetWorldToReferenceOrientation(ignition::math::Quaterniond::Identity);
  }

  sensor->SetActive(true);

  if (!LoadParameters())
  {
    ROS_FATAL("Error Loading Parameters!");
    return;
  }

  if (!ros::isInitialized())  // check if ros is initialized properly
  {
    ROS_FATAL("ROS has not been initialized!");
    return;
  }

  node = new ros::NodeHandle(this->robot_namespace);

  imu_data_publisher = node->advertise<sensor_msgs::Imu>(topic_name, 1);

  connection = gazebo::event::Events::ConnectWorldUpdateBegin(boost::bind(&GazeboRosImuSensor::UpdateChild, this, _1));

  last_time = sensor->LastUpdateTime();
}

void gazebo::GazeboRosImuSensor::UpdateChild(const gazebo::common::UpdateInfo& /*_info*/)
{
#ifdef ENABLE_PROFILER
  IGN_PROFILE("GazeboRosImuSensor::UpdateChild");
#endif
  common::Time current_time = sensor->LastUpdateTime();

  if (update_rate > 0 && (current_time - last_time).Double() < 1.0 / update_rate)  // update rate check
    return;

  if (imu_data_publisher.getNumSubscribers() > 0)
  {
#ifdef ENABLE_PROFILER
    IGN_PROFILE_BEGIN("fill ROS message");
#endif
    orientation = offset.Rot() * sensor->Orientation();  // applying offsets to the orientation measurement
    accelerometer_data = sensor->LinearAcceleration();
    gyroscope_data = sensor->AngularVelocity();

    // Guassian noise is applied to all measurements
    imu_msg.orientation.x = orientation.X();
    imu_msg.orientation.y = orientation.Y();
    imu_msg.orientation.z = orientation.Z();
    imu_msg.orientation.w = orientation.W();

    imu_msg.linear_acceleration.x = accelerometer_data.X();
    imu_msg.linear_acceleration.y = accelerometer_data.Y();
    imu_msg.linear_acceleration.z = accelerometer_data.Z();

    imu_msg.angular_velocity.x = gyroscope_data.X();
    imu_msg.angular_velocity.y = gyroscope_data.Y();
    imu_msg.angular_velocity.z = gyroscope_data.Z();

    imu_msg.orientation_covariance[0] = orientation_variance.X();
    imu_msg.orientation_covariance[4] = orientation_variance.Y();
    imu_msg.orientation_covariance[8] = orientation_variance.Z();
    imu_msg.angular_velocity_covariance[0] = gyroscope_variance.X();
    imu_msg.angular_velocity_covariance[4] = gyroscope_variance.Y();
    imu_msg.angular_velocity_covariance[8] = gyroscope_variance.Z();
    imu_msg.linear_acceleration_covariance[0] = acceleration_variance.X();
    imu_msg.linear_acceleration_covariance[4] = acceleration_variance.Y();
    imu_msg.linear_acceleration_covariance[8] = acceleration_variance.Z();

    // preparing message header
    imu_msg.header.frame_id = body_name;
    imu_msg.header.stamp.sec = current_time.sec;
    imu_msg.header.stamp.nsec = current_time.nsec;
#ifdef ENABLE_PROFILER
    IGN_PROFILE_END();
    // publishing data
    IGN_PROFILE_BEGIN("publish");
#endif
    imu_data_publisher.publish(imu_msg);
#ifdef ENABLE_PROFILER
    IGN_PROFILE_END();
#endif
    ros::spinOnce();
  }

  last_time = current_time;
}

bool gazebo::GazeboRosImuSensor::LoadParameters()
{
  // loading parameters from the sdf file

  // NAMESPACE
  if (sdf->HasElement("robotNamespace"))
  {
    robot_namespace = sdf->Get<std::string>("robotNamespace") + "/";
    ROS_INFO_STREAM("<robotNamespace> set to: " << robot_namespace);
  }
  else
  {
    std::string scoped_name = sensor->ParentName();
    std::size_t it = scoped_name.find("::");

    robot_namespace = "/" + scoped_name.substr(0, it) + "/";
    ROS_WARN_STREAM("missing <robotNamespace>, set to default: " << robot_namespace);
  }

  // TOPIC
  if (sdf->HasElement("topicName"))
  {
    topic_name = sdf->Get<std::string>("topicName");
    ROS_INFO_STREAM("<topicName> set to: " << topic_name);
  }
  else
  {
    topic_name = "imu_data";
    ROS_WARN_STREAM("missing <topicName>, set to /namespace/default: " << topic_name);
  }

  // BODY NAME
  if (sdf->HasElement("frameName"))
  {
    body_name = sdf->Get<std::string>("frameName");
    ROS_INFO_STREAM("<frameName> set to: " << body_name);
  }
  else
  {
    ROS_FATAL("missing <frameName>, cannot proceed");
    return false;
  }

  // UPDATE RATE
  if (sdf->HasElement("updateRateHZ"))
  {
    update_rate = sdf->Get<double>("updateRateHZ");
    ROS_INFO_STREAM("<updateRateHZ> set to: " << update_rate);
  }
  else
  {
    update_rate = 1.0;
    ROS_WARN_STREAM("missing <updateRateHZ>, set to default: " << update_rate);
  }

  // ORIENTATION COVARIANCE
  if (sdf->HasElement("orientationCovariance"))
  {
    orientation_variance = sdf->Get<ignition::math::Vector3d>("orientationCovariance");
    ROS_INFO_STREAM("<orientationCovariance> set to: " << orientation_variance);
  }
  else
  {
    orientation_variance = ignition::math::Vector3d(0, 0, 0);
    ROS_WARN_STREAM("missing <orientationCovariance>, set to default: " << orientation_variance);
  }

  // LINEAR ACCELERATION COVARIANCE
  if (sdf->HasElement("linearAccelerationCovariance"))
  {
    acceleration_variance = sdf->Get<ignition::math::Vector3d>("linearAccelerationCovariance");
    ROS_INFO_STREAM("<linearAccelerationCovariance> set to: " << acceleration_variance);
  }
  else
  {
    acceleration_variance = ignition::math::Vector3d(0, 0, 0);
    ROS_WARN_STREAM("missing <linearAccelerationCovariance>, set to default: " << acceleration_variance);
  }

  // ANGULAR VELOCITY COVARIANCE
  if (sdf->HasElement("angularVelocityCovariance"))
  {
    gyroscope_variance = sdf->Get<ignition::math::Vector3d>("angularVelocityCovariance");
    ROS_INFO_STREAM("<angularVelocityCovariance> set to: " << gyroscope_variance);
  }
  else
  {
    gyroscope_variance = ignition::math::Vector3d(0, 0, 0);
    ROS_WARN_STREAM("missing <angularVelocityCovariance>, set to default: " << gyroscope_variance);
  }

  // POSITION OFFSET, UNUSED
  if (sdf->HasElement("xyzOffset"))
  {
    offset.Pos() = sdf->Get<ignition::math::Vector3d>("xyzOffset");
    ROS_INFO_STREAM("<xyzOffset> set to: " << offset.Pos()[0] << ' ' << offset.Pos()[1] << ' ' << offset.Pos()[2]);
  }
  else
  {
    offset.Pos() = ignition::math::Vector3d(0, 0, 0);
    ROS_WARN_STREAM("missing <xyzOffset>, set to default: " << offset.Pos()[0] << ' ' << offset.Pos()[1] << ' '
                                                            << offset.Pos()[2]);
  }

  // ORIENTATION OFFSET
  if (sdf->HasElement("rpyOffset"))
  {
    offset.Rot() = ignition::math::Quaterniond(sdf->Get<ignition::math::Vector3d>("rpyOffset"));
    ROS_INFO_STREAM("<rpyOffset> set to: " << offset.Rot().Roll() << ' ' << offset.Rot().Pitch() << ' '
                                           << offset.Rot().Yaw());
  }
  else
  {
    offset.Rot() = ignition::math::Quaterniond::Identity;
    ROS_WARN_STREAM("missing <rpyOffset>, set to default: " << offset.Rot().Roll() << ' ' << offset.Rot().Pitch() << ' '
                                                            << offset.Rot().Yaw());
  }

  return true;
}

gazebo::GazeboRosImuSensor::~GazeboRosImuSensor()
{
  if (connection.get())
  {
    connection.reset();
    connection = gazebo::event::ConnectionPtr();
  }

  node->shutdown();
}