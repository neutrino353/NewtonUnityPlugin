/* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 
* 3. This notice may not be removed or altered from any source distribution.
*/

#include "stdafx.h"
#include "dNewtonBody.h"
#include "dNewtonWorld.h"
#include "dNewtonCollision.h"


bool dNewtonBody::GetSleepState() const
{
	return NewtonBodyGetSleepState(m_body) ? true : false;
}

void dNewtonBody::SetSleepState(bool state) const
{
	NewtonBodySetSleepState(m_body, state ? 1 : 0);
}


dNewtonBody::ScopeLock::ScopeLock (unsigned int* const lock)
	:m_atomicLock(lock)
{
	const int maxCount = 1024 * 32;
	for (int i = 0; (i < maxCount) && NewtonAtomicSwap((int*)m_atomicLock, 1); i++) {
		NewtonYield();
	}
}

dNewtonBody::ScopeLock::~ScopeLock()
{
	NewtonAtomicSwap((int*)m_atomicLock, 0);
}


dNewtonBody::dNewtonBody(const dMatrix& matrix)
	:dAlloc()
	,m_body(NULL)
	,m_posit0(matrix.m_posit)
	,m_posit1(matrix.m_posit)
	,m_interpolatedPosit(matrix.m_posit)
	,m_rotation0(matrix)
	,m_rotation1(m_rotation0)
	,m_interpolatedRotation(m_rotation0)
	,m_lock(0)
	,m_onCollision(NULL)
{
}

dNewtonBody::~dNewtonBody()
{
	Destroy();
}

void dNewtonBody::SetCallbacks(OnCollisionCallback collisionCallback)
{
	m_onCollision = collisionCallback;
}

void* dNewtonBody::GetBody() const
{
	return m_body;
}

void* dNewtonBody::GetPosition()
{
	const dNewtonWorld* const world = (dNewtonWorld*)NewtonWorldGetUserData(NewtonBodyGetWorld(m_body));
	ScopeLock scopelock(&m_lock);
	m_interpolatedPosit = m_posit0 + (m_posit1 - m_posit0).Scale(world->m_interpotationParam);
	return &m_interpolatedPosit.m_x;
}

void* dNewtonBody::GetRotation()
{
	const dNewtonWorld* const world = (dNewtonWorld*) NewtonWorldGetUserData(NewtonBodyGetWorld(m_body));
	ScopeLock scopelock(&m_lock);
	m_interpolatedRotation = m_rotation0.Slerp(m_rotation1, world->m_interpotationParam);
	return &m_interpolatedRotation.m_q0;
}

void dNewtonBody::OnBodyTransform(const dFloat* const matrixPtr, int threadIndex)
{
	dMatrix matrix(matrixPtr);

	ScopeLock scopelock(&m_lock);
	m_posit0 = m_posit1;
	m_rotation0 = m_rotation1;
	m_posit1 = matrix.m_posit;
	m_rotation1 = dQuaternion(matrix);
	dFloat angle = m_rotation0.DotProduct(m_rotation1);
	if (angle < 0.0f) {
		m_rotation1.Scale(-1.0f);
	}
}

void dNewtonBody::OnForceAndTorque(dFloat timestep, int threadIndex)
{
}

void dNewtonBody::OnForceAndTorqueCallback (const NewtonBody* body, dFloat timestep, int threadIndex)
{
	dNewtonBody* const me = (dNewtonBody*)NewtonBodyGetUserData(body);
	dAssert(me);
	me->OnForceAndTorque(timestep, threadIndex);
}


void dNewtonBody::OnBodyTransformCallback (const NewtonBody* const body, const dFloat* const matrix, int threadIndex)
{
	dNewtonBody* const me = (dNewtonBody*)NewtonBodyGetUserData(body);
	dAssert(me);
	me->OnBodyTransform(matrix, threadIndex);
}


void dNewtonBody::Destroy()
{
//	if (m_body && NewtonBodyGetDestructorCallback(m_body)) {
	if (m_body) {
		NewtonBodySetDestructorCallback(m_body, NULL);
		NewtonDestroyBody(m_body);
		m_body = NULL;
	}
}

void dNewtonBody::OnBodyDestroy(const NewtonBody* const body)
{
	dAssert(0);
/*
	dNewtonBody* const me = (dNewtonBody*)NewtonBodyGetUserData(body);
	if (me) {
		NewtonBodySetDestructorCallback(me->m_body, NULL);
		delete me;
	}
*/
}

void dNewtonBody::InitForceAccumulators()
{
}

void dNewtonBody::AddForce(dVector force)
{
}

void dNewtonBody::AddTorque(dVector torque)
{
}

dNewtonDynamicBody::dNewtonDynamicBody(dNewtonWorld* const world, dNewtonCollision* const collision, dMatrix matrix, dFloat mass)
	:dNewtonBody(matrix)
{
	NewtonWorld* const newton = world->m_world;
	m_body = NewtonCreateDynamicBody(newton, collision->m_shape, &matrix[0][0]);
	collision->DeleteShape();
	collision->SetShape(NewtonBodyGetCollision(m_body));

	NewtonBodySetMassProperties(m_body, mass, NewtonBodyGetCollision(m_body));

	NewtonBodySetUserData(m_body, this);
	NewtonBodySetTransformCallback(m_body, OnBodyTransformCallback);
	NewtonBodySetForceAndTorqueCallback(m_body, OnForceAndTorqueCallback);
}

void dNewtonDynamicBody::InitForceAccumulators()
{
	dFloat mass;
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;

	NewtonBodyGetMass(m_body, &mass, &Ixx, &Iyy, &Izz);
	const dNewtonWorld* const world = (dNewtonWorld*)NewtonWorldGetUserData(NewtonBodyGetWorld(m_body));

	m_externalForce = world->GetGravity().Scale(mass);
	m_externalTorque = dVector(0.0f);
}


void dNewtonDynamicBody::OnForceAndTorque(dFloat timestep, int threadIndex)
{
	NewtonBodySetForce(m_body, &m_externalForce[0]);
	NewtonBodySetTorque(m_body, &m_externalTorque[0]);
}


void dNewtonDynamicBody::AddForce(dVector force)
{
	m_externalForce += force;
}

void dNewtonDynamicBody::AddTorque(dVector torque)
{
	m_externalTorque += torque;
}


