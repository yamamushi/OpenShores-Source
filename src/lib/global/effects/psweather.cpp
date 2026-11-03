//-+---+---+---+---+---+---+---+---+---|---+---+---+---+---+---+-==+---+---+---|---+---+---+---+---+---+---+---+---+---+---+---+---|
// $Id: psweather.cpp,v 1.22 2022/02/19 18:34:21 daveh Exp $
// Copyright (c) 2022 Hazeron Freehold LLC
// All rights reserved.
//-+---+---+---+---+---+---+---+---+---|---+---+---+---+---+---+-==+---+---+---|---+---+---+---+---+---+---+---+---+---+---+---+---|

#include "psweather.h"
//
#include "augleffects.h"

PsWeather::PsWeather() : AuParticleSystem()
{
	m_vWind	= AuPoint(0.0f,0.0f,0.0f);
	m_sphereConstraintLength = 0.0f;
	m_renderMethod = RENDER_METHOD_Snow;
}

PsWeather::~PsWeather()
{

}

void PsWeather::MoveParticle(auParticle* pParticle)
{
	// Update position with respect to velocity
	if(!m_tFix.IsIdentity())
	{
		pParticle->m_vCurPos = m_tFix.Project(pParticle->m_vCurPos);
		pParticle->m_vCurVel = m_tFix.ProjectVector(pParticle->m_vCurVel);
	}
	pParticle->m_vOldPos = pParticle->m_vCurPos;
	pParticle->m_vCurPos += (pParticle->m_vCurVel + m_vWind) * GetElapsedTime();

	// If sphere constraint is present then determine outcome of particle
	double distToViewer = pParticle->m_vCurPos.Length2();
	if(distToViewer <= m_sphereConstraintLength || distToViewer > pParticle->m_InitDist)
		pParticle->m_fInitTime -= GetLifeCycle();	// Particle needs to die next pass
}

void PsWeather::InitParticle(auParticle* pParticle)
{
	pParticle->m_vCurVel = m_gN * GetVelocity().z;
	if(GetVelocityVar() != 0)
		pParticle->m_vCurVel += GetRandomVector() * GetVelocityVar();
	pParticle->m_fInitTime = GetCurTime();
	pParticle->m_vCurPos = GetPosition();
	if(GetPositionVar() != 0)
	{
		// Offset the position by a random vector.
		AuPoint randv(GetRandomVector());
		randv.x *= GetPositionVar();
		randv.y *= GetPositionVar();
		randv.z = (randv.z * GetPositionVar()/2) + GetPositionVar();	// Move up, with variance, so first snow layer is not flat.
		pParticle->m_vCurPos += randv;
	}
	if(!m_tNew.IsIdentity())
		pParticle->m_vCurPos = m_tNew.Project(pParticle->m_vCurPos);
	double distToViewer = pParticle->m_vCurPos.Length2();
	if(distToViewer < m_sphereConstraintLength)
	{
		double l = sqrt(m_sphereConstraintLength) + 1;
		pParticle->m_vCurPos.Unit(l);
	}
	pParticle->m_InitDist = pParticle->m_vCurPos.Length2();	// Particle is killed when it moves farther away than it started.
	pParticle->m_vOldPos = pParticle->m_vCurPos;
}

void PsWeather::Render(const AuMatrix4x4& projection, const AuMatrix4x4& model, AuGLEffects& re, float scale)
{
	Q_UNUSED(projection);
	Q_UNUSED(scale);
	if(m_renderMethod == RENDER_METHOD_Snow || m_renderMethod == RENDER_METHOD_Snow_Ringworld)
	{
		QVector<GLfloat> positions;
		for(auParticle* particle = GetActiveList(); particle; particle=particle->m_pNext)
		{
			AuPoint viewPt = model.Project(particle->m_vCurPos);
			if(viewPt.z >= 0)
				continue;
			viewPt.Unit();
			if(viewPt.Dot(AuPoint(0,0,-1)) < .7071)
				continue;
			positions << GLfloat(particle->m_vCurPos.x) << GLfloat(particle->m_vCurPos.y)
				<< GLfloat(particle->m_vCurPos.z) << 1.f;
		}
		re.RenderSnow(positions);
	}
	else if(m_renderMethod == RENDER_METHOD_Rain)
	{
		// Render each particle as a line segment.
		// Compile a list of vertex locations.
		QVector<GLfloat> verts;
		for(auParticle* pParticle = GetActiveList(); pParticle; pParticle=pParticle->m_pNext)
		{
			// Skip the particles behind the viewer.
			AuPoint viewPt = model.Project(pParticle->m_vCurPos);
			if(viewPt.z >= 0)
				continue;
			else
			{
				viewPt.Unit();
				if(viewPt.Dot(AuPoint(0,0,-1)) < .7071)
					continue;
			}

			AuPoint vDir(pParticle->m_vOldPos - pParticle->m_vCurPos);
			vDir.Unit(.01);
			AuPoint vOldPos(pParticle->m_vOldPos - vDir);

			verts << GLfloat(vOldPos.x) << GLfloat(vOldPos.y) << GLfloat(vOldPos.z) << 1.f;
			verts << GLfloat(pParticle->m_vCurPos.x) << GLfloat(pParticle->m_vCurPos.y)
				<< GLfloat(pParticle->m_vCurPos.z) << 1.f;
		}

		re.RenderParticleStream(verts, GL_LINES);
	}
}
