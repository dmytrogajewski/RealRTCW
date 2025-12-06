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

// SSAO framebuffer
static struct {
	GLuint fbo;
	GLuint texture;
	int width;
	int height;
	qboolean initialized;
} ssaoFBO;

// Exported texture handle
GLuint ssaoTexture = 0;

// CVars are declared in tr_local.h

static int InvertMatrix4x4( const float *m, float *out ) {
    float inv[16], det;
    int i;

    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5] * m[15] + 
             m[8]  * m[7] * m[13] + 
             m[12] * m[5] * m[11] - 
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] + 
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] - 
               m[8]  * m[6] * m[13] - 
               m[12] * m[5] * m[10] + 
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2] * m[15] - 
              m[9]  * m[3] * m[14] - 
              m[13] * m[2] * m[11] + 
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2] * m[15] + 
             m[8]  * m[3] * m[14] + 
             m[12] * m[2] * m[11] - 
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1] * m[15] - 
              m[8]  * m[3] * m[13] - 
              m[12] * m[1] * m[11] + 
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1] * m[14] + 
              m[8]  * m[2] * m[13] + 
              m[12] * m[1] * m[10] - 
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] - 
             m[1]  * m[7] * m[14] - 
             m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + 
             m[13] * m[2] * m[7] - 
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] + 
              m[0]  * m[7] * m[14] + 
              m[4]  * m[2] * m[15] - 
              m[4]  * m[3] * m[14] - 
              m[12] * m[2] * m[7] + 
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] - 
              m[0]  * m[7] * m[13] - 
              m[4]  * m[1] * m[15] + 
              m[4]  * m[3] * m[13] + 
              m[12] * m[1] * m[7] - 
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] + 
               m[0]  * m[6] * m[13] + 
               m[4]  * m[1] * m[14] - 
               m[4]  * m[2] * m[13] - 
               m[12] * m[1] * m[6] + 
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + 
              m[1] * m[7] * m[10] + 
              m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - 
              m[9] * m[2] * m[7] + 
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - 
             m[0] * m[7] * m[10] - 
             m[4] * m[2] * m[11] + 
             m[4] * m[3] * m[10] + 
             m[8] * m[2] * m[7] - 
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + 
               m[0] * m[7] * m[9] + 
               m[4] * m[1] * m[11] - 
               m[4] * m[3] * m[9] - 
               m[8] * m[1] * m[7] + 
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - 
              m[0] * m[6] * m[9] - 
              m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9] + 
              m[8] * m[1] * m[6] - 
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (det == 0)
        return 0;

    det = 1.0 / det;

    for (i = 0; i < 16; i++)
        out[i] = inv[i] * det;

    return 1;
}

/*
=================
R_InitSSAO
=================
*/
qboolean R_InitSSAO( void ) {
	GLenum status;
	
	if ( !r_ssao->integer ) {
		ssaoFBO.initialized = qfalse;
		return qfalse;
	}
	
	if ( !tr.glslAvailable || !tr.ssaoShader || !tr.ssaoShader->compiled ) {
		ri.Printf( PRINT_WARNING, "SSAO requires GLSL shaders\n" );
		ssaoFBO.initialized = qfalse;
		return qfalse;
	}
	
	if ( !qglGenFramebuffers || !qglBindFramebuffer ) {
		ri.Printf( PRINT_WARNING, "SSAO requires framebuffer objects\n" );
		ssaoFBO.initialized = qfalse;
		return qfalse;
	}
	
	// Use half resolution for performance
	ssaoFBO.width = glConfig.vidWidth / 2;
	ssaoFBO.height = glConfig.vidHeight / 2;
	
	// Generate framebuffer
	qglGenFramebuffers( 1, &ssaoFBO.fbo );
	qglBindFramebuffer( GL_FRAMEBUFFER, ssaoFBO.fbo );
	
	// Create SSAO texture
	qglGenTextures( 1, &ssaoFBO.texture );
	ssaoTexture = ssaoFBO.texture;
	qglBindTexture( GL_TEXTURE_2D, ssaoFBO.texture );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RED,
		ssaoFBO.width, ssaoFBO.height,
		0, GL_RED, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	// Attach texture
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, ssaoFBO.texture, 0 );
	
	// Check status
	status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( status != GL_FRAMEBUFFER_COMPLETE ) {
		ri.Printf( PRINT_WARNING, "SSAO framebuffer incomplete: 0x%x\n", status );
		R_ShutdownSSAO();
		return qfalse;
	}
	
	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	ssaoFBO.initialized = qtrue;
	
	ri.Printf( PRINT_ALL, "SSAO initialized: %dx%d\n", ssaoFBO.width, ssaoFBO.height );
	
	return qtrue;
}

/*
=================
R_ShutdownSSAO
=================
*/
void R_ShutdownSSAO( void ) {
	if ( ssaoFBO.texture ) {
		qglDeleteTextures( 1, &ssaoFBO.texture );
		ssaoFBO.texture = 0;
	}
	
	if ( ssaoFBO.fbo ) {
		qglDeleteFramebuffers( 1, &ssaoFBO.fbo );
		ssaoFBO.fbo = 0;
	}
	
	ssaoFBO.initialized = qfalse;
}

/*
=================
R_RenderSSAO
=================
*/
void R_RenderSSAO( void ) {
	if ( !r_ssao->integer || !ssaoFBO.initialized || !tr.ssaoShader || !tr.ssaoShader->compiled ) {
		return;
	}
	
	if ( !tr.hdrAvailable || !tr.hdrFramebuffer.initialized ) {
		return; // SSAO requires depth/normal textures from HDR framebuffer
	}
	
	// Bind SSAO framebuffer
	qglBindFramebuffer( GL_FRAMEBUFFER, ssaoFBO.fbo );
	qglViewport( 0, 0, ssaoFBO.width, ssaoFBO.height );
	
	// Clear
	qglClear( GL_COLOR_BUFFER_BIT );
	
	// Use SSAO shader
	R_UseGLSLProgram( tr.ssaoShader );
	
	// Set uniforms
	if ( tr.ssaoShader->u_depthTexture >= 0 ) {
		qglActiveTexture( GL_TEXTURE0 );
		qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.depthTexture );
		qglUniform1i( tr.ssaoShader->u_depthTexture, 0 );
	}
	
	if ( tr.ssaoShader->u_normalTexture >= 0 ) {
		qglActiveTexture( GL_TEXTURE1 );
		qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.normalTexture );
		qglUniform1i( tr.ssaoShader->u_normalTexture, 1 );
	}
	
	// Set projection matrix for depth reconstruction
	if ( tr.ssaoShader->u_projectionMatrix >= 0 ) {
		qglUniformMatrix4fv( tr.ssaoShader->u_projectionMatrix, 1, GL_FALSE, tr.viewParms.projectionMatrix );
	}
	
	// Set inverse projection matrix for position reconstruction
	if ( tr.ssaoShader->u_invProjectionMatrix >= 0 ) {
		float invProj[16];
		if ( InvertMatrix4x4( tr.viewParms.projectionMatrix, invProj ) ) {
			qglUniformMatrix4fv( tr.ssaoShader->u_invProjectionMatrix, 1, GL_FALSE, invProj );
		}
	}
	
	// Set SSAO parameters
	if ( tr.ssaoShader->u_screenSize >= 0 ) {
		vec2_t screenSize;
		screenSize[0] = (float)ssaoFBO.width;
		screenSize[1] = (float)ssaoFBO.height;
		qglUniform2fv( tr.ssaoShader->u_screenSize, 1, screenSize );
	}
	
	if ( tr.ssaoShader->u_radius >= 0 ) {
		qglUniform1f( tr.ssaoShader->u_radius, r_ssaoRadius->value );
	}
	
	if ( tr.ssaoShader->u_bias >= 0 ) {
		qglUniform1f( tr.ssaoShader->u_bias, r_ssaoBias->value );
	}
	
	if ( tr.ssaoShader->u_intensity >= 0 ) {
		qglUniform1f( tr.ssaoShader->u_intensity, r_ssaoIntensity->value );
	}
	
	// Disable depth testing
	qglDisable( GL_DEPTH_TEST );
	
	// Render fullscreen quad
	R_RenderFullscreenQuad();
	
	// Restore state
	R_UnuseGLSLProgram();
	
	// Restore HDR framebuffer if enabled, otherwise unbind
	if ( tr.hdrAvailable && tr.hdrFramebuffer.initialized ) {
		qglBindFramebuffer( GL_FRAMEBUFFER, tr.hdrFramebuffer.fbo );
	} else {
		qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
	
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
}

