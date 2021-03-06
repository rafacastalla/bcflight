#ifndef MULTICOPTER_H
#define MULTICOPTER_H

#include "Frame.h"

class Multicopter : public Frame
{
public:
	Multicopter( Config* config );
	~Multicopter();

	void Arm();
	void Disarm();
	void WarmUp();
	virtual bool Stabilize( const Vector3f& pid_output, const float& thrust );

	static Frame* Instanciate( Config* config );
	static int flight_register( Main* main );

protected:
	std::vector< float > mStabSpeeds;
	std::vector< Vector3f > mPIDMultipliers;
	float mMaxSpeed;
	float mAirModeTrigger;
	float mAirModeSpeed;
};

#endif // MULTICOPTER_H
