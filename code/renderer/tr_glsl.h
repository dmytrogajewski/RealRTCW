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

#ifndef TR_GLSL_H
#define TR_GLSL_H

#include "tr_local.h"

// GLSL Shader Program Structure
typedef struct glslProgram_s {
	GLuint program;
	GLuint vertexShader;
	GLuint fragmentShader;
	
	char name[MAX_QPATH];
	
	// Common uniform locations
	GLint u_modelMatrix;
	GLint u_viewMatrix;
	GLint u_projectionMatrix;
	GLint u_modelViewMatrix;
	GLint u_modelViewProjectionMatrix;
	GLint u_normalMatrix;
	
	GLint u_time;
	GLint u_viewOrigin;
	GLint u_viewAxis;
	
	// Material uniforms
	GLint u_diffuseMap;
	GLint u_normalMap;
	GLint u_specularMap;
	GLint u_roughnessMap;
	GLint u_metallicMap;
	GLint u_emissiveMap;
	
	GLint u_baseColor;
	GLint u_roughness;
	GLint u_metallic;
	GLint u_emissive;
	
	// Lighting uniforms
	GLint u_ambientLight;
	GLint u_directedLight;
	GLint u_lightDir;
	GLint u_lightColor;
	GLint u_lightPosition;
	GLint u_lightRadius;
	
	// Shadow uniforms
	GLint u_shadowMap;
	GLint u_shadowMatrix;
	GLint u_shadowCascadeDistances;
	
	// Post-processing uniforms
	GLint u_screenTexture;
	GLint u_depthTexture;
	GLint u_normalTexture;
	GLint u_ssaoTexture; // Added
	GLint u_exposure;
	GLint u_gamma;
	GLint u_screenSize;
	GLint u_radius;
	GLint u_bias;
	GLint u_intensity;
	GLint u_useSSAO;
	GLint u_invProjectionMatrix; // Added
	
	qboolean compiled;
	qboolean inUse;
	
	struct glslProgram_s *next;
} glslProgram_t;

// Shader cache entry
typedef struct glslShaderCache_s {
	char name[MAX_QPATH];
	glslProgram_t *program;
	struct glslShaderCache_s *next;
} glslShaderCache_t;

// HDR Framebuffer Structure
typedef struct hdrFramebuffer_s {
	GLuint fbo;
	GLuint colorTexture;
	GLuint depthTexture;
	GLuint normalTexture;  // For deferred rendering
	GLuint positionTexture; // For deferred rendering
	
	int width;
	int height;
	
	qboolean initialized;
} hdrFramebuffer_t;

// Post-processing framebuffer chain
typedef struct postProcessFBO_s {
	GLuint fbo;
	GLuint texture;
	int width;
	int height;
	struct postProcessFBO_s *next;
} postProcessFBO_t;

// Function declarations
qboolean R_InitGLSL( void );
void R_ShutdownGLSL( void );

glslProgram_t *R_FindGLSLProgram( const char *name );
glslProgram_t *R_CreateGLSLProgram( const char *name, const char *vertexSource, const char *fragmentSource );
void R_UseGLSLProgram( glslProgram_t *program );
void R_UnuseGLSLProgram( void );

qboolean R_InitHDRFramebuffers( void );
void R_ShutdownHDRFramebuffers( void );
void R_BindHDRFramebuffer( void );
void R_UnbindHDRFramebuffer( void );
void R_BlitHDRToScreen( void );

// Post-processing
void R_ApplyToneMapping( void );
void R_RenderFullscreenQuad( void );

// Shadow mapping
qboolean R_InitShadowMaps( void );
void R_ShutdownShadowMaps( void );
void R_RenderShadowMaps( void );

// SSAO
qboolean R_InitSSAO( void );
void R_ShutdownSSAO( void );
void R_RenderSSAO( void );

// Shader source loading
char *R_LoadShaderSource( const char *filename );
qboolean R_CompileShader( GLuint shader, const char *source );
qboolean R_LinkProgram( GLuint program, GLuint vertexShader, GLuint fragmentShader );

// Note: Built-in shader programs and HDR framebuffer are in trGlobals_t (tr.worldShader, etc.)
// CVars are declared in tr_glsl.c

#endif // TR_GLSL_H

