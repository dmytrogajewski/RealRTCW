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

// Cascaded Shadow Map (CSM) structure
#define MAX_SHADOW_CASCADES 4

typedef struct shadowCascade_s {
	GLuint shadowMap;
	GLuint fbo;
	int width;
	int height;
	float splitDistance;
	vec3_t lightDir;
	vec3_t lightPos;
	float lightRadius;
	float projectionMatrix[16];
	float viewMatrix[16];
	float lightSpaceMatrix[16];
	qboolean initialized;
} shadowCascade_t;

typedef struct shadowSystem_s {
	shadowCascade_t cascades[MAX_SHADOW_CASCADES];
	int numCascades;
	int shadowMapSize;
	qboolean enabled;
} shadowSystem_t;

static shadowSystem_t shadowSystem;

// CVars are declared in tr_local.h

/*
=================
R_InitShadowMaps
=================
*/
qboolean R_InitShadowMaps( void ) {
	int i;
	GLenum status;
	
	if ( !r_shadows->integer || r_shadows->integer < 2 ) {
		// Use old stencil shadows
		shadowSystem.enabled = qfalse;
		return qfalse;
	}
	
	if ( !tr.glslAvailable || !tr.shadowMapShader || !tr.shadowMapShader->compiled ) {
		ri.Printf( PRINT_WARNING, "Shadow maps require GLSL shaders\n" );
		shadowSystem.enabled = qfalse;
		return qfalse;
	}
	
	if ( !qglGenFramebuffers || !qglBindFramebuffer ) {
		ri.Printf( PRINT_WARNING, "Shadow maps require framebuffer objects\n" );
		shadowSystem.enabled = qfalse;
		return qfalse;
	}
	
	shadowSystem.shadowMapSize = r_shadowMapSize->integer;
	if ( shadowSystem.shadowMapSize < 256 ) {
		shadowSystem.shadowMapSize = 256;
	}
	if ( shadowSystem.shadowMapSize > 4096 ) {
		shadowSystem.shadowMapSize = 4096;
	}
	
	shadowSystem.numCascades = r_shadowCascades->integer;
	if ( shadowSystem.numCascades < 1 ) {
		shadowSystem.numCascades = 1;
	}
	if ( shadowSystem.numCascades > MAX_SHADOW_CASCADES ) {
		shadowSystem.numCascades = MAX_SHADOW_CASCADES;
	}
	
	// Initialize cascades
	for ( i = 0; i < shadowSystem.numCascades; i++ ) {
		shadowCascade_t *cascade = &shadowSystem.cascades[i];
		
		// Generate framebuffer
		qglGenFramebuffers( 1, &cascade->fbo );
		qglBindFramebuffer( GL_FRAMEBUFFER, cascade->fbo );
		
		// Generate shadow map texture
		qglGenTextures( 1, &cascade->shadowMap );
		qglBindTexture( GL_TEXTURE_2D, cascade->shadowMap );
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
			shadowSystem.shadowMapSize, shadowSystem.shadowMapSize,
			0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL );
		
		// Set texture parameters for shadow mapping
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		// Use CLAMP_TO_EDGE if border color not available
		#ifndef GL_TEXTURE_BORDER_COLOR
		#define GL_TEXTURE_BORDER_COLOR 0x1004
		#endif
		if ( QGL_VERSION_ATLEAST( 1, 3 ) ) {
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
			
			// Set border color to white (no shadow)
			float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			qglTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor );
		} else {
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}
		
		// Attach depth texture to framebuffer
		qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_TEXTURE_2D, cascade->shadowMap, 0 );
		
		// Disable color rendering for shadow map
		qglDrawBuffer( GL_NONE );
		qglReadBuffer( GL_NONE );
		
		// Check framebuffer status
		status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
		if ( status != GL_FRAMEBUFFER_COMPLETE ) {
			ri.Printf( PRINT_WARNING, "Shadow map framebuffer incomplete: 0x%x\n", status );
			shadowSystem.enabled = qfalse;
			qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
			return qfalse;
		}
		
		cascade->width = shadowSystem.shadowMapSize;
		cascade->height = shadowSystem.shadowMapSize;
		cascade->initialized = qtrue;
	}
	
	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	shadowSystem.enabled = qtrue;
	
	ri.Printf( PRINT_ALL, "Shadow maps initialized: %d cascades, %dx%d\n",
		shadowSystem.numCascades, shadowSystem.shadowMapSize, shadowSystem.shadowMapSize );
	
	return qtrue;
}

/*
=================
R_ShutdownShadowMaps
=================
*/
void R_ShutdownShadowMaps( void ) {
	int i;
	
	for ( i = 0; i < MAX_SHADOW_CASCADES; i++ ) {
		shadowCascade_t *cascade = &shadowSystem.cascades[i];
		
		if ( cascade->shadowMap ) {
			qglDeleteTextures( 1, &cascade->shadowMap );
			cascade->shadowMap = 0;
		}
		
		if ( cascade->fbo ) {
			qglDeleteFramebuffers( 1, &cascade->fbo );
			cascade->fbo = 0;
		}
		
		cascade->initialized = qfalse;
	}
	
	shadowSystem.enabled = qfalse;
}

/*
=================
R_CalculateShadowCascades
=================
*/
static void R_CalculateShadowCascades( const viewParms_t *viewParms ) {
	int i;
	float cascadeSplits[MAX_SHADOW_CASCADES];
	float zNear = r_znear->value;
	float zFar = viewParms->zFar;
	float lambda = 0.5f; // Cascade distribution parameter
	
	// Calculate split distances
	for ( i = 0; i < shadowSystem.numCascades; i++ ) {
		float ratio = (float)(i + 1) / shadowSystem.numCascades;
		float logSplit = zNear * pow(zFar / zNear, ratio);
		float uniformSplit = zNear + (zFar - zNear) * ratio;
		cascadeSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
	}
	
	// Calculate light direction from sun
	VectorCopy( tr.sunDirection, shadowSystem.cascades[0].lightDir );
	
	// For each cascade, calculate light space matrix
	for ( i = 0; i < shadowSystem.numCascades; i++ ) {
		shadowCascade_t *cascade = &shadowSystem.cascades[i];
		float splitDist = cascadeSplits[i];
		
		// Store split distance
		cascade->splitDistance = splitDist;
		
		// Calculate frustum corners for this cascade
		// This is a simplified version - full implementation would calculate
		// actual frustum corners and fit a bounding box
		
		// For now, use a simple orthographic projection
		float radius = splitDist * 0.5f;
		VectorSet( cascade->lightPos, 
			tr.refdef.vieworg[0] + cascade->lightDir[0] * radius,
			tr.refdef.vieworg[1] + cascade->lightDir[1] * radius,
			tr.refdef.vieworg[2] + cascade->lightDir[2] * radius );
		cascade->lightRadius = radius * 2.0f;
	}
}

/*
=================
R_RenderShadowMaps
=================
*/
void R_RenderShadowMaps( void ) {
	int i;
	
	if ( !shadowSystem.enabled ) {
		return;
	}
	
	// Calculate cascade splits and light matrices
	R_CalculateShadowCascades( &tr.viewParms );
	
	// Use shadow shader
	R_UseGLSLProgram( tr.shadowMapShader );
	
	// Render each cascade
	for ( i = 0; i < shadowSystem.numCascades; i++ ) {
		shadowCascade_t *cascade = &shadowSystem.cascades[i];
		
		if ( !cascade->initialized ) {
			continue;
		}
		
		// Bind shadow map framebuffer
		qglBindFramebuffer( GL_FRAMEBUFFER, cascade->fbo );
		qglViewport( 0, 0, cascade->width, cascade->height );
		
		// Clear depth buffer
		qglClear( GL_DEPTH_BUFFER_BIT );
		
		// Set up light space matrix (simplified - would need proper calculation)
		// This is a placeholder - full implementation requires proper frustum fitting
		
		// Render scene from light's perspective
		// This would call the normal rendering functions but with light's view
		// For now, this is a framework for future implementation
	}
	
	// Restore default framebuffer
	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	
	R_UnuseGLSLProgram();
}

