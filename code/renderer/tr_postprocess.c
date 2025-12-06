/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code ("RTCW SP Source Code").  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "tr_local.h"

// Fullscreen quad vertices for post-processing
static const float fullscreenQuad[] = {
	-1.0f, -1.0f,
	 1.0f, -1.0f,
	-1.0f,  1.0f,
	 1.0f,  1.0f
};

/*
=================
R_ApplyToneMapping
=================
*/
void R_ApplyToneMapping( void ) {
	if ( !tr.hdrAvailable || !tr.tonemapShader || !tr.tonemapShader->compiled ) {
		return;
	}
	
	// Unbind HDR framebuffer
	R_UnbindHDRFramebuffer();
	
	// Set up 2D rendering
	RB_SetGL2D();
	
	// Use tone mapping shader
	R_UseGLSLProgram( tr.tonemapShader );
	
	// Set uniforms
	if ( tr.tonemapShader->u_screenTexture >= 0 ) {
		qglActiveTexture( GL_TEXTURE0 );
		qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.colorTexture );
		qglUniform1i( tr.tonemapShader->u_screenTexture, 0 );
	}
	
	// Bind SSAO texture if enabled
	if ( r_ssao->integer && ssaoTexture && tr.tonemapShader->u_ssaoTexture >= 0 ) {
		qglActiveTexture( GL_TEXTURE1 );
		qglBindTexture( GL_TEXTURE_2D, ssaoTexture );
		qglUniform1i( tr.tonemapShader->u_ssaoTexture, 1 );
		
		if ( tr.tonemapShader->u_useSSAO >= 0 ) {
			qglUniform1f( tr.tonemapShader->u_useSSAO, 1.0f );
		}
	} else if ( tr.tonemapShader->u_useSSAO >= 0 ) {
		qglUniform1f( tr.tonemapShader->u_useSSAO, 0.0f );
	}
	
	if ( tr.tonemapShader->u_exposure >= 0 ) {
		qglUniform1f( tr.tonemapShader->u_exposure, r_hdrExposure->value );
	}
	
	if ( tr.tonemapShader->u_gamma >= 0 ) {
		qglUniform1f( tr.tonemapShader->u_gamma, r_hdrGamma->value );
	}
	
	// Disable depth testing and blending for fullscreen quad
	qglDisable( GL_DEPTH_TEST );
	qglDisable( GL_BLEND );
	
	// Draw fullscreen quad
	R_RenderFullscreenQuad();
	
	// Restore state
	R_UnuseGLSLProgram();
}

/*
=================
R_RenderFullscreenQuad
=================
*/
void R_RenderFullscreenQuad( void ) {
	// Manually disable common arrays to avoid crashes
	qglDisableClientState( GL_NORMAL_ARRAY );
	qglDisableClientState( GL_COLOR_ARRAY );
	
	// Disable texture coord arrays on used units (0 and 1 usually)
	if ( qglClientActiveTextureARB ) {
		qglClientActiveTextureARB( GL_TEXTURE0_ARB );
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		
		qglClientActiveTextureARB( GL_TEXTURE1_ARB );
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		
		// Restore to unit 0
		qglClientActiveTextureARB( GL_TEXTURE0_ARB );
	} else {
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}
	
	qglEnableClientState( GL_VERTEX_ARRAY );
	qglVertexPointer( 2, GL_FLOAT, 0, fullscreenQuad );
	qglDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	qglDisableClientState( GL_VERTEX_ARRAY );
}

