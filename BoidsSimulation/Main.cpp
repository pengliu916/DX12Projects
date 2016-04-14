#include "stdafx.h"
#include "BoidsSimulation.h"

_Use_decl_annotations_
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow )
{
	BoidsSimulation application( 1280, 720);
	return Core::Run( application, hInstance, nCmdShow );
}
