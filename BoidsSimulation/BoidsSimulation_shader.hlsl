#include "BoidsSimulation_SharedHeader.inl"

StructuredBuffer<FishData> oldVP : register(t0);
RWStructuredBuffer<FishData> newPosVel : register(u0);
Texture2D txColor : register(t1);
Texture2D txFish : register(t2);
SamplerState samGeneral : register(s0);

#if COMPUTE_SHADER 
// Shader static constant
static float softeningSquared = 0.0012500000*0.0012500000;
static float softening = 0.0012500000;
static float bondaryFactor = 10.0f;

//--------------------------------------------------------------------------------------
// Utility Functions for Compute Shader
//--------------------------------------------------------------------------------------
// Calculate the force from border
float3 BoundaryCorrection( float3 localPos, float3 localVel )
{
	float3 probePos = localPos + localVel * 0.5;
	return probePos;
}

// Calculate the force to avoid other fish
float3 Avoidance( float3 localPos, float3 velDir, float3 avoidPos, float distSqr )
{
	float3 OP = avoidPos - localPos;
	float t = dot( OP, velDir );
	float3 tPos = localPos + velDir * t;
	float3 force = tPos - avoidPos;
	float forceLenSqr = dot( force, force ) + softeningSquared;
	return fAvoidanceFactor * force / (forceLenSqr * distSqr);
}

// Calculate separation force
float3 Seperation( float3 neighborDir, float3 neighborVel, float invDist )
{
	float3 neighborVelSqr = dot( neighborVel, neighborVel ) + softeningSquared;
	float3 invNeighborVelLen = 1.0f / sqrt( neighborVelSqr );
	float3 neighborVelDir = neighborVel * invNeighborVelLen;
	float directionFactor = abs( dot( neighborDir, neighborVelDir ) ) + softening;
	return -fSeperationFactor * neighborDir * invDist * invDist * (1 + 3 * directionFactor);
}

// Calculate cohesion force
float3 Cohesion( float3 localPos, float3 avgPos )
{
	float3 delta = avgPos - localPos;
	float deltaSqr = dot( delta, delta ) + softeningSquared;
	float invDelta = 1.0f / sqrt( deltaSqr );
	return fCohesionFactor * delta * invDelta;
}

// Calculate alignment force
float3 Alignment( float3 localVel, float3 avgVel )
{
	float3 delta = avgVel - localVel;
	float deltaSqr = dot( delta, delta ) + softeningSquared;
	float invDelta = 1.0f / sqrt( deltaSqr );
	return fAlignmentFactor * delta * invDelta;
}

// Calculate seeking force
float3 Seekings( float3 localPos, float3 localVel, float3 seekPos )
{
	float3 delta = seekPos - localPos;
	float deltaSqr = dot( delta, delta ) + softeningSquared;
	float invDelta = 1.0f / sqrt( deltaSqr );
	float3 desiredVel = delta * invDelta * fMaxSpeed - localVel;
	return fSeekingFactor * delta *invDelta;
}

float3 Seeking( float3 vLocalPos, float3 vLocalVel, float3 vSeekPos )
{
	float3 vDelta = normalize( vSeekPos - vLocalPos );
	float3 vDesired = vDelta * fMaxSpeed;
	return fSeekingFactor*(vDesired - vLocalVel);
}

// Calculate flee force
float3 Flee( float3 localPos, float3 localVel, float3 fleePos )
{
	float3 delta = localPos - fleePos;
	float deltaSqr = dot( delta, delta ) + softeningSquared;
	float invDelta = 1.0f / sqrt( deltaSqr );
	float3 desiredVel = delta * fMaxSpeed;
	return fFleeFactor*(desiredVel - localVel) *invDelta*invDelta;
}

// Calculate border force from planes
void PlaneVelCorrection( inout float3 probePos )
{
	float3 diff = max( float3(0, 0, 0), probePos - f3xyzExpand );
	probePos = probePos - diff;
	return;
}

// Calculate border force from edges
void EdgeVelCorrection( float2 probeToEdge, float cornerRadius, inout float2 probePos )
{
	if (all( probeToEdge < int2(0, 0) )) {
		float dist = length( probeToEdge );
		if (dist > cornerRadius) {
			float2 moveDir = normalize( probeToEdge );
			probePos += moveDir * (dist - cornerRadius);
		}
	}
	return;
}

// Calculate border force from corners
void CornerVelCorrection( float3 probeToCorner, float cornerRadius, inout float3 probPos )
{
	if (all( probeToCorner < int3(0, 0, 0) )) {
		float dist = length( probeToCorner );
		if (dist > cornerRadius) {
			float3 moveDir = normalize( probeToCorner );
			probPos += moveDir * (dist - cornerRadius);
		}
	}
	return;
}

void BorderVelCorrection( float3 pos, inout float3 vel )
{
	float speed = length( vel );
	float probeDist = speed * 25 * fDeltaT;
	float3 probePos = pos + 25 * fDeltaT * vel;
	float cornerRadius = probeDist * 1.5f;
	int3 posState = pos > float3(0, 0, 0);
	int3 convert = posState * 2 - int3(1, 1, 1);
	float3 mirrorProbePos = probePos * convert;
	float3 cornerSphereCenterPos = f3xyzExpand - cornerRadius;
	float3 probeToCorner = cornerSphereCenterPos - mirrorProbePos;
	// For corners
	CornerVelCorrection( probeToCorner, cornerRadius, mirrorProbePos );
	// For edges
	EdgeVelCorrection( probeToCorner.xy, cornerRadius, mirrorProbePos.xy );
	EdgeVelCorrection( probeToCorner.xz, cornerRadius, mirrorProbePos.xz );
	EdgeVelCorrection( probeToCorner.yz, cornerRadius, mirrorProbePos.yz );
	// For planes
	PlaneVelCorrection( mirrorProbePos );
	// Get true new probe pos
	probePos = mirrorProbePos * convert;
	// Get new vel
	vel = speed * normalize( probePos - pos );
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------
// To improve I/O operations load data into group shared memory
groupshared FishData sharedFishVP[BLOCK_SIZE];

[numthreads( BLOCK_SIZE, 1, 1 )]
void csmain( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
	// Each thread of the CS updates one fish
	FishData localVP = oldVP[DTid.x];
	localVP.pos -= f3CenterPos;			// Transform to local space
	float predatorFactor = 1;
	float vDT = fVisionDist;
	float vAT = fVisionAngleCos;

	// Local variable to store temporary values
	float3	accForce = 0;			// Keep track of all forces for this fish
	float3	accPos = 0;			// Accumulate neighbor fish pos for neighbor ave pos calculation
	float3	accVel = 0;			// Accumulate neighbor fish vel for neighbor ave vel calculation
	uint	accCount = 0;			// Number of near by fish (neighbor) for ave data calculation

	float scalarVel = sqrt( dot( localVP.vel, localVP.vel ) );
	float3 velDir = localVP.vel / scalarVel;
	// Update current fish using all other fish
	[loop]
	for (uint tile = 0; tile <= uint(uNumInstance / BLOCK_SIZE); tile++)
	{
		// Cache a tile of particles onto shared memory to increase IO efficiency
		uint currentTileStart = tile * BLOCK_SIZE;
		sharedFishVP[GI] = oldVP[tile * BLOCK_SIZE + GI];
		sharedFishVP[GI].pos -= f3CenterPos;	// Transform to local space

		// Set a group barrier to make sure the entitle shared memory is loaded with data
		GroupMemoryBarrierWithGroupSync();

		//[unroll]
		for (uint counter = 0; counter < BLOCK_SIZE; counter++)
		{
			// Calculate distance
			float3 vPos = sharedFishVP[counter].pos - localVP.pos;
			float distSqr = dot( vPos, vPos ) + softeningSquared;
			float dist = sqrt( distSqr );
			float invDist = 1.0f / dist;
			// Calculate angle between vel and dist dir
			float3 neighborDir = vPos * invDist;

			float cosAngle = dot( velDir, neighborDir );
			// Doing one to one interaction based on visibility
			if (dist <= vDT && cosAngle >= vAT)
			{
				accPos += sharedFishVP[counter].pos;	// accumulate neighbor fish pos
				accVel += sharedFishVP[counter].vel;	// accumulate neighbor fish vel
				accCount += 1;							// counting neighbors for calculating avg PV
														// Add separation and avoidance force
				accForce += Seperation( neighborDir, sharedFishVP[counter].vel, invDist )*predatorFactor;
				accForce += Avoidance( localVP.pos, velDir, sharedFishVP[counter].pos, distSqr )*predatorFactor;
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	// Calculate average pos and vel of neighbor fish
	float3 avgPos = float3(0, 0, 0);
	if (accCount != 0) {
		avgPos = accPos / accCount;
		float3 avgVel = accVel / accCount;
		float3 localFleeSourcePos = f3FleeSourcePos - f3CenterPos; // Convert flee source pos to local space
															   // Add cohesion alignment forces
		accForce += Cohesion( localVP.pos, avgPos + normalize( localVP.vel )*0.2f );
		accForce += Alignment( localVP.vel, avgVel )*predatorFactor;
		accForce += Flee( localVP.pos, localVP.vel, localFleeSourcePos )*predatorFactor;
	}

	float maxForce;
	float maxSpeed;
	float minSpeed;
	float3 seekPos;

	{
		seekPos = f3SeekSourcePos - f3CenterPos;	// Convert seek source pos to local space
		maxForce = fMaxForce;
		maxSpeed = fMaxSpeed;
		minSpeed = fMinSpeed;
	}
	float3 seek = Seeking( localVP.pos, localVP.vel, seekPos );
	accForce += seek;
	//accForce.y*=0.8;
	float accForceSqr = dot( accForce, accForce ) + softeningSquared;
	float invForceLen = 1.0f / sqrt( accForceSqr );
	float3 forceDir = accForce * invForceLen;
	if (accForceSqr > maxForce * maxForce)
	{
		accForce = forceDir*fMaxForce;
	}

	localVP.vel += accForce * fDeltaT;      //deltaTime;
	float velAfterSqr = dot( localVP.vel, localVP.vel );
	float invVelLen = 1.0f / sqrt( velAfterSqr );
	if (velAfterSqr > maxSpeed * maxSpeed)
	{
		localVP.vel = localVP.vel * invVelLen * maxSpeed;
	}
	else if (velAfterSqr < minSpeed * minSpeed)
	{
		localVP.vel = localVP.vel * invVelLen * minSpeed;
	}
	BorderVelCorrection( localVP.pos, localVP.vel );
	//localVP.vel = BoundaryCorrection( localVP.pos, localVP.vel );
	localVP.pos.xyz += localVP.vel.xyz * fDeltaT;        //deltaTime;    

	newPosVel[DTid.x].pos = localVP.pos + f3CenterPos;	// Convert the result pos back to world space
	newPosVel[DTid.x].vel = localVP.vel;
}
#endif

#if GRAPHICS_SHADER
//--------------------------------------------------------------------------------------
// Structs for Render
//--------------------------------------------------------------------------------------
struct VS_Input
{
	float3 pos : POSITION;
	float2 tex : TEXCOORD;
	uint	instanceID : SV_INSTANCEID;	// Used for Shark identification(First 2 instance)
};

struct GS_Input
{
	float3 pos : POSITION;
	float2 tex : TEXCOORD;
	uint	instanceID : SV_INSTANCEID;
};

struct PS_Input
{
	float4	Pos		: SV_POSITION;
	float2	Tex		: TEXCOORD;
	float	Illu : ILLUMI;			// Lighting done in GS
	float	Vidx : VINDEX;			// Index for velocity color mapping
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
GS_Input vsmain( GS_Input In )
{
	GS_Input output = In;
	return output;
}

//--------------------------------------------------------------------------------------
// Geometry Shader
//--------------------------------------------------------------------------------------
[maxvertexcount( 3 )]
void gsmain( triangle GS_Input input[3], inout TriangleStream<PS_Input> SpriteStream )
{
	PS_Input output;
	float3 vertex[3];
	float3 pos = oldVP[input[0].instanceID].pos;
	float3 vel = oldVP[input[0].instanceID].vel;

	/*	int3 state = pos > float3( 0, 0, 0 );
	state = state * 2 - int3( 1, 1, 1 );
	pos *= state;
	vel *= state;*/

	float velLen = length( vel );

	vel /= velLen;										// Forward vector for each fish
	float3 fup = float3(0, 1, 0);						// Faked up vector for each fish
	float3 right = cross( fup, vel );						// Right vector for each fish
	float3 up = cross( right, vel );
	float3x3 rotMatrix = {vel, up, right};			// Rotation matrix to pose each fish heading forward
	rotMatrix = transpose( rotMatrix );

	float size = fFishSize;							// Set the fish size

														// Calculate color index for later velocity color mapping
	float vidx = (velLen - fMinSpeed) / (fMaxSpeed - fMinSpeed);

	[unroll]for (int i = 0; i<3; i++)
	{	// Pose each fish and scale it based on its size
		vertex[i] = mul( rotMatrix, input[i].pos*float3(1.5, 1, 1)*size );
	}
	// Calculate the normals
	float3 v0 = vertex[1] - vertex[0];
	float3 v1 = vertex[2] - vertex[0];
	float3 n = normalize( cross( v0, v1 ) );
	// Calculate lighting
	float3 vLight = float3(0.4242641, 0.5656854, 0.7071068);
	float illumination = abs( dot( n, vLight ) );

	[unroll]for (int j = 0; j<3; j++)
	{
		output.Pos = mul( mWorldViewProj, float4(vertex[j] + pos, 1));
		output.Tex = input[j].tex;
		output.Vidx = vidx;
		output.Illu = illumination;

		SpriteStream.Append( output );
	}
	SpriteStream.RestartStrip();
}
//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 getColor( Texture2D tex, float2 coord ) {
	//return tex.Load( int3(coord*float2(260, 640),0) );
	//return tex.Sample( samGeneral, float2(0.5f,0.5f) );
	return tex.Sample( samGeneral, coord );
}

float4 psmain( PS_Input In ) : SV_Target
{
	//return float4(1,0,0,0);
	//float4 col = txColor.Sample(samGeneral, float2(0.48,In.Vidx));
	float4 col = getColor( txColor, float2(0.48,In.Vidx) );
	return  col*In.Illu;
}
#endif