
#include <string>
#include <exception>
#include <std_msgs/String.h>
#include <ensenso/HeadPose.h>
#include <geometry_msgs/Twist.h>
#include <std_msgs/Float64MultiArray.h>
#include "nn_controller/nn_controller.h"

using namespace amfc_control;

//constructor
Controller::Controller(ros::NodeHandle nc, const Eigen::Vector3d& ref)
: n_(nc), ref_(ref), updatePoseInfo(false), updateController(false),
 updateWeights(false), print(false), counter(0), multicast_address("235.255.0.1")
{	 		 
	initMatrices();
	pred_pub_ = n_.advertise<nn_controller::predictor>("/osa_pred", 10);
	control_pub_ = n_.advertise<geometry_msgs::Twist>("/mannequine_head/u_valves", 100);
}

//copy constructor
Controller::Controller()
{
	start = std::chrono::high_resolution_clock::now();
}

// Destructor.
Controller::~Controller()
{
}

ros::Time Controller::getTime()
{
	return ros::Time::now();
}

/*Subscribers*/
// pose subscriber from ensenso_seg
void Controller::pose_subscriber(const ensenso::HeadPose& headPose)
{
	Eigen::VectorXd pose_info;
	pose_info.resize(3);		//CRITICAL OR SEGFAULT
	getPoseInfo(headPose, pose_info);

	std::lock_guard<std::mutex> pose_locker(pose_mutex);
	this->pose_info = pose_info;
	updatePoseInfo  = true;		
}

//net subscriber from sample.lua in RAL/farnn
void Controller::ref_model_multisub(const std_msgs::Float64MultiArray::ConstPtr& ref_model_params)
{
	std::vector<double> model_params = ref_model_params->data;

	//retrieve the pre-trained weights and biases 
	Eigen::Matrix<double, 3, 3> modelWeights;
	Eigen::Vector3d modelBiases;
	std::lock_guard<std::mutex> weights_lock(weights_mutex);
	{	
		modelWeights(0,0) = model_params[0];
		modelWeights(0,1) = model_params[1];
		modelWeights(0,2) = model_params[2];
		modelWeights(1,0) = model_params[4];
		modelWeights(1,1) = model_params[5];
		modelWeights(1,2) = model_params[6];
		modelWeights(2,0) = model_params[8];
		modelWeights(2,1) = model_params[9];
		modelWeights(2,2) = model_params[10];

		modelBiases(0)	  = model_params[3];
		modelBiases(1)	  = model_params[7];
		modelBiases(2)	  = model_params[11];

		this->modelWeights = modelWeights;
		this->modelBiases  = modelBiases;

		updateWeights = true;
	}
}


void Controller::getPoseInfo(const ensenso::HeadPose& headPose, Eigen::VectorXd pose_info)
{
	pose_info << //headPose.x, headPose.y,
				 headPose.z,// 1,  		//roll to zero
				 headPose.pitch, headPose.yaw;   //setting roll to zero
	//set ref's non-controlled states to measurement
	ControllerParams(std::move(pose_info));
}

//real-time predictor inputs service request response ::DEPRECATED
bool Controller::configure_predictor_params(
	nn_controller::predictor_params::Request  &req,
	nn_controller::predictor_params::Response  &res)
{
	Eigen::VectorXd u_control_local;
	if(updateController)
	{
		u_control_local = this->u_control;
		updateController = false;
	}
	
	Eigen::Vector3d pose_info;
	pose_info.resize(3);
	pose_info = this->pose_info;

	if(updatePoseInfo)
	{
		pose_info = this->pose_info;
		updatePoseInfo = false;
	}

	// pose_info << headPose.z,// 1,  		//roll to zero
	// 			 headPose.pitch, headPose.yaw;   //setting roll to zero
	// res.header.seq = counter;
	// res.header.stamp = getTime();
	// res.header.frame_id = "net_predictor_input";
	res.u1 		=	u_control_local(0);
	res.u2 		=	u_control_local(1);
	res.u3		=	u_control_local(2);
	res.u4		=	u_control_local(3);
	res.u5		=	u_control_local(4);
	res.u6		=	u_control_local(5);
	//measurements
	res.z		=	pose_info(0);
	res.pitch	=	pose_info(1);
	res.yaw		=	pose_info(2);

	OUT("\npose_info: " << pose_info);

	return true;
}

//error service response generator ::DEPRECATED
bool Controller::configure_error(
	nn_controller::amfcError::Request  &req,
	nn_controller::amfcError::Response  &res)
{
	// this is the reference model. it is always time-varying1
	// k_m = 1.25;  
	// a_m = -0.782404601/k;   
	// y_0 = 0;  //assume zero initial response
 	// now = std::chrono::high_resolution_clock::now();
 	// double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() / 1000.0;
	// y_m = k_m * ref * exp(-a_m *k*T);

	// ROS_INFO_STREAM(" ym: " << y_m);
	// //parametric error between ref. model and plant
	// error = linear.z - y_m;
	// res.error = error;
	// //publish am, km, ref and y as services as well
	// res.am 	= a_m;
	// res.km 	= k_m;
	// res.ref = ref;
	// res.y  	= linear.z;
	// ROS_INFO_STREAM("sending response: " << res);	
	// ++k;

	return true;
}

void Controller::pred_subscriber(const geometry_msgs::Point& pred)
{
	std::lock_guard<std::mutex> net_pred_locker(pred_mutex);
	this->pred.resize(6);

	this->pred << pred.x, pred.y, pred.z,
					0,		0,		0;
	updatePred = true;
}

void Controller::loss_subscriber(const std_msgs::Float64& net_loss)
{
	std::lock_guard<std::mutex> net_loss_locker(net_loss_mutex);
	this->loss = net_loss.data;
	updateNetLoss = true;
}

void Controller::ControllerParams(Eigen::VectorXd&& pose_info)
{	
	tracking_error.resize(3);
	expAmk *= std::exp(-1334./1705*counter);
	ym = Bm * ref_ * expAmk;

	//compute tracking error, e = y - y_m
	tracking_error = pose_info - ym;
	Ky_hat = -1* Gamma_y * pose_info * tracking_error.transpose().eval() * P * B;
	Kr_hat = -1* Gamma_r * ref_      * tracking_error.transpose().eval() * P * B;
	//retrieve the first 6x3 block from Kr and Kx in place
	Ky_hat = Ky_hat.topLeftCorner(6,3);
	/*Will be 6 x 3*/
	Kr_hat = Kr_hat.topLeftCorner(6,3);

	//retrieve net predictions
	Eigen::VectorXd pred;
	pred.resize(6);
	if(updatePred)
	{
		std::lock_guard<std::mutex> net_pred_locker(pred_mutex);
		updatePred = false;
		pred = this->pred;
	}

	//retrieve predictor loss
	double loss;
	if(updateNetLoss)
	{
		std::lock_guard<std::mutex> net_loss_locker(net_loss_mutex);
		updateNetLoss = false;
		loss = this->loss;
	}

	//SEE HEADER FOR INFO ON SIZE OF MATRICES
	u_control = (Ky_hat * pose_info) + (Kr_hat * ref_) + pred;
	{
		std::lock_guard<std::mutex> lock(mutex);
		this->u_control = u_control;
		updateController = true;
	}

	OUT("\n pred: \n" << pred.transpose());
	OUT("\nControl Law: \n" << u_control.transpose());

	if(print){		
		// OUT("\nym: \n" << ym.transpose());
		OUT("\nKy_hat: \n" << Ky_hat);
		OUT("\nKr_hat: \n" << Kr_hat);
		OUT("\ntracking_error: \n" << tracking_error.transpose());
		OUT("\nControl Law: \n" << u_control.transpose());
	}

	geometry_msgs::Twist u_valves;
	u_valves.linear.x  = u_control(0);
	u_valves.linear.y  = u_control(1);
	u_valves.linear.z  = u_control(2);
	u_valves.angular.x = u_control(3);
	u_valves.angular.y = u_control(4);
	u_valves.angular.z = u_control(5);

	control_pub_.publish(u_valves);
	//convert from eigen to headpose
	ensenso::HeadPose eig2Pose;
	vectorToHeadPose(std::move(pose_info), eig2Pose);
	//fallback since rosrio is messing up
	udp::sender s(io_service, boost::asio::ip::address::from_string(multicast_address), 
	        	  u_valves, ref_, eig2Pose);

	++counter;
}

bool Controller::configure_controller(
	nn_controller::controller::Request  &req,
	nn_controller::controller::Response  &res)
{
	Eigen::VectorXd u_control_local;
	if(updateController)
	{
		u_control_local = this->u_control;
		updateController = false;
	}

	res.left_in 	=	u_control_local(0);
	res.left_out 	=	u_control_local(1);
	res.right_in	=	u_control_local(2);
	res.right_out	=	u_control_local(3);
	res.base_in		=	u_control_local(4);
	res.base_out	=	u_control_local(5);

	return true;
}
void Controller::vectorToHeadPose(Eigen::VectorXd&& pose_info, ensenso::HeadPose& eig2Pose)
{
    eig2Pose.z = pose_info(0);
    eig2Pose.pitch = pose_info(1);
    eig2Pose.yaw = pose_info(2);
}

void help()
{
	OUT("\t\tAdd the 3DOF desired trajectory separated by a single space");
	OUT("\t\tLike so: rosrun nn_controller nn_controller <z> <pitch> <yaw>");
}

int main(int argc, char** argv)
{ 
	ros::init(argc, argv, "controller_node", ros::init_options::AnonymousName);
	ros::NodeHandle n;

	Eigen::Vector3f ref;
	// ref.resize(3);
	if(argc < 2)
	{
		help();
		return EXIT_FAILURE;
	} 
	try{		
		ref(0) = atof(argv[1]);    //ref z
		ref(1) = atof(argv[2]);	  //ref pitch
		ref(2) = atof(argv[3]);	  //ref yaw
	}
	catch(std::exception& e){
		e.what();
	}


	Eigen::Vector3d refd;
	refd = ref.cast<double>(); 
	Controller c(n, refd);

    ros::Subscriber sub_multi = n.subscribe("/mannequine_pred/net_weights", 1000, &Controller::ref_model_multisub, &c );
	ros::Subscriber sub_pose = n.subscribe("/mannequine_head/pose", 100, &Controller::pose_subscriber, &c);	
	//subscribe to real -time predictor parameters
	ros::Subscriber sub_pred = n.subscribe("/mannequine_pred/preds", 100, &Controller::pred_subscriber, &c);
	ros::Subscriber sub_loss = n.subscribe("/mannequine_pred/net_loss", 100, &Controller::loss_subscriber, &c);
	ros::ServiceServer control_serv = n.advertiseService("/mannequine_head/controller", 
										&Controller::configure_controller, &c);
	//not working
	// ros::ServiceServer pred_serv = n.advertiseService("/mannequine_head/predictor_params", 
										// &Controller::configure_predictor_params, &c);

	ros::spin();

	ros::shutdown();

	return 0;
}