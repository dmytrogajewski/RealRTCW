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

// CVars
cvar_t *r_glsl;
cvar_t *r_hdr;
cvar_t *r_hdrExposure;
cvar_t *r_hdrGamma;
cvar_t *r_tonemap;
cvar_t *r_ssao;
cvar_t *r_pbr;
cvar_t *r_normalMapping;
cvar_t *r_specularMapping;

// Shader cache
#define MAX_GLSL_PROGRAMS 256
static glslProgram_t glslPrograms[MAX_GLSL_PROGRAMS];
static int numGLSLPrograms = 0;
static glslShaderCache_t *glslShaderCache = NULL;

// Current active program
static glslProgram_t *currentProgram = NULL;

/*
=================
R_CheckGLSLSupport
=================
*/
static qboolean R_CheckGLSLSupport( void ) {
	if ( !QGL_VERSION_ATLEAST( 2, 0 ) && !QGLES_VERSION_ATLEAST( 2, 0 ) ) {
		ri.Printf( PRINT_WARNING, "OpenGL version is below 2.0, GLSL not supported\n" );
		return qfalse;
	}
	
	// Try to load GLSL function pointers (may not be loaded in fixed-function mode)
	if ( !GLimp_LoadGLSLProcs() ) {
		ri.Printf( PRINT_WARNING, "Failed to load OpenGL 2.0 functions for GLSL\n" );
		return qfalse;
	}
	
	// Verify required functions are available
	if ( !qglCreateProgram || !qglCreateShader || !qglShaderSource ) {
		ri.Printf( PRINT_WARNING, "Required GLSL functions not available\n" );
		return qfalse;
	}
	
	return qtrue;
}

/*
=================
R_CompileShader
=================
*/
qboolean R_CompileShader( GLuint shader, const char *source ) {
	GLint compiled;
	const char *sourcePtr = source;
	
	qglShaderSource( shader, 1, &sourcePtr, NULL );
	qglCompileShader( shader );
	
	qglGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( !compiled ) {
		GLint logLength;
		char *log;
		
		qglGetShaderiv( shader, GL_INFO_LOG_LENGTH, &logLength );
		log = ri.Hunk_AllocateTempMemory( logLength + 1 );
		qglGetShaderInfoLog( shader, logLength, NULL, log );
		
		ri.Printf( PRINT_WARNING, "Shader compilation failed:\n%s\n", log );
		ri.Hunk_FreeTempMemory( log );
		
		return qfalse;
	}
	
	return qtrue;
}

/*
=================
R_LinkProgram
=================
*/
qboolean R_LinkProgram( GLuint program, GLuint vertexShader, GLuint fragmentShader ) {
	GLint linked;
	
	qglAttachShader( program, vertexShader );
	qglAttachShader( program, fragmentShader );
	qglLinkProgram( program );
	
	qglGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( !linked ) {
		GLint logLength;
		char *log;
		
		qglGetProgramiv( program, GL_INFO_LOG_LENGTH, &logLength );
		log = ri.Hunk_AllocateTempMemory( logLength + 1 );
		qglGetProgramInfoLog( program, logLength, NULL, log );
		
		ri.Printf( PRINT_WARNING, "Program linking failed:\n%s\n", log );
		ri.Hunk_FreeTempMemory( log );
		
		return qfalse;
	}
	
	return qtrue;
}

/*
=================
R_LoadShaderSource
=================
*/
char *R_LoadShaderSource( const char *filename ) {
	char *buffer;
	int len;
	char fullPath[MAX_QPATH];
	
	// Try loading from game directory first
	Com_sprintf( fullPath, sizeof(fullPath), "%s", filename );
	len = ri.FS_ReadFile( fullPath, (void **)&buffer );
	if ( buffer && len > 0 ) {
		return buffer;
	}
	
	// Try with shaders/ prefix
	Com_sprintf( fullPath, sizeof(fullPath), "shaders/%s", filename );
	len = ri.FS_ReadFile( fullPath, (void **)&buffer );
	if ( buffer && len > 0 ) {
		return buffer;
	}
	
	ri.Printf( PRINT_WARNING, "Failed to load shader: %s\n", filename );
	return NULL;
}

/*
=================
R_CreateGLSLProgram
=================
*/
glslProgram_t *R_CreateGLSLProgram( const char *name, const char *vertexSource, const char *fragmentSource ) {
	glslProgram_t *program;
	GLuint vertexShader, fragmentShader;
	
	if ( numGLSLPrograms >= MAX_GLSL_PROGRAMS ) {
		ri.Printf( PRINT_WARNING, "R_CreateGLSLProgram: MAX_GLSL_PROGRAMS reached\n" );
		return NULL;
	}
	
	program = &glslPrograms[numGLSLPrograms++];
	Com_Memset( program, 0, sizeof( glslProgram_t ) );
	
	Q_strncpyz( program->name, name, sizeof( program->name ) );
	
	// Create shaders
	vertexShader = qglCreateShader( GL_VERTEX_SHADER );
	fragmentShader = qglCreateShader( GL_FRAGMENT_SHADER );
	
	if ( !vertexShader || !fragmentShader ) {
		ri.Printf( PRINT_WARNING, "R_CreateGLSLProgram: Failed to create shaders\n" );
		return NULL;
	}
	
	// Compile shaders
	if ( !R_CompileShader( vertexShader, vertexSource ) ) {
		qglDeleteShader( vertexShader );
		qglDeleteShader( fragmentShader );
		return NULL;
	}
	
	if ( !R_CompileShader( fragmentShader, fragmentSource ) ) {
		qglDeleteShader( vertexShader );
		qglDeleteShader( fragmentShader );
		return NULL;
	}
	
	// Create and link program
	program->program = qglCreateProgram();
	program->vertexShader = vertexShader;
	program->fragmentShader = fragmentShader;
	
	if ( !R_LinkProgram( program->program, vertexShader, fragmentShader ) ) {
		qglDeleteShader( vertexShader );
		qglDeleteShader( fragmentShader );
		qglDeleteProgram( program->program );
		return NULL;
	}
	
	// Get common uniform locations
	program->u_modelMatrix = qglGetUniformLocation( program->program, "u_modelMatrix" );
	program->u_viewMatrix = qglGetUniformLocation( program->program, "u_viewMatrix" );
	program->u_projectionMatrix = qglGetUniformLocation( program->program, "u_projectionMatrix" );
	program->u_modelViewMatrix = qglGetUniformLocation( program->program, "u_modelViewMatrix" );
	program->u_modelViewProjectionMatrix = qglGetUniformLocation( program->program, "u_modelViewProjectionMatrix" );
	program->u_normalMatrix = qglGetUniformLocation( program->program, "u_normalMatrix" );
	
	program->u_time = qglGetUniformLocation( program->program, "u_time" );
	program->u_viewOrigin = qglGetUniformLocation( program->program, "u_viewOrigin" );
	program->u_viewAxis = qglGetUniformLocation( program->program, "u_viewAxis" );
	
	// Material uniforms
	program->u_diffuseMap = qglGetUniformLocation( program->program, "u_diffuseMap" );
	program->u_normalMap = qglGetUniformLocation( program->program, "u_normalMap" );
	program->u_specularMap = qglGetUniformLocation( program->program, "u_specularMap" );
	program->u_roughnessMap = qglGetUniformLocation( program->program, "u_roughnessMap" );
	program->u_metallicMap = qglGetUniformLocation( program->program, "u_metallicMap" );
	program->u_emissiveMap = qglGetUniformLocation( program->program, "u_emissiveMap" );
	
	program->u_baseColor = qglGetUniformLocation( program->program, "u_baseColor" );
	program->u_roughness = qglGetUniformLocation( program->program, "u_roughness" );
	program->u_metallic = qglGetUniformLocation( program->program, "u_metallic" );
	program->u_emissive = qglGetUniformLocation( program->program, "u_emissive" );
	
	// Lighting uniforms
	program->u_ambientLight = qglGetUniformLocation( program->program, "u_ambientLight" );
	program->u_directedLight = qglGetUniformLocation( program->program, "u_directedLight" );
	program->u_lightDir = qglGetUniformLocation( program->program, "u_lightDir" );
	program->u_lightColor = qglGetUniformLocation( program->program, "u_lightColor" );
	program->u_lightPosition = qglGetUniformLocation( program->program, "u_lightPosition" );
	program->u_lightRadius = qglGetUniformLocation( program->program, "u_lightRadius" );
	
	// Shadow uniforms
	program->u_shadowMap = qglGetUniformLocation( program->program, "u_shadowMap" );
	program->u_shadowMatrix = qglGetUniformLocation( program->program, "u_shadowMatrix" );
	program->u_shadowCascadeDistances = qglGetUniformLocation( program->program, "u_shadowCascadeDistances" );
	
	// Post-processing uniforms
	program->u_screenTexture = qglGetUniformLocation( program->program, "u_screenTexture" );
	program->u_depthTexture = qglGetUniformLocation( program->program, "u_depthTexture" );
	program->u_normalTexture = qglGetUniformLocation( program->program, "u_normalTexture" );
	program->u_ssaoTexture = qglGetUniformLocation( program->program, "u_ssaoTexture" );
	program->u_exposure = qglGetUniformLocation( program->program, "u_exposure" );
	program->u_gamma = qglGetUniformLocation( program->program, "u_gamma" );
	program->u_screenSize = qglGetUniformLocation( program->program, "u_screenSize" );
	program->u_radius = qglGetUniformLocation( program->program, "u_radius" );
	program->u_bias = qglGetUniformLocation( program->program, "u_bias" );
	program->u_intensity = qglGetUniformLocation( program->program, "u_intensity" );
	program->u_useSSAO = qglGetUniformLocation( program->program, "u_useSSAO" );
	program->u_invProjectionMatrix = qglGetUniformLocation( program->program, "u_invProjectionMatrix" );
	
	program->compiled = qtrue;
	
	ri.Printf( PRINT_ALL, "Created GLSL program: %s\n", name );
	
	return program;
}

/*
=================
R_FindGLSLProgram
=================
*/
glslProgram_t *R_FindGLSLProgram( const char *name ) {
	int i;
	
	for ( i = 0; i < numGLSLPrograms; i++ ) {
		if ( !Q_stricmp( glslPrograms[i].name, name ) ) {
			return &glslPrograms[i];
		}
	}
	
	return NULL;
}

/*
=================
R_UseGLSLProgram
=================
*/
void R_UseGLSLProgram( glslProgram_t *program ) {
	if ( !program || !program->compiled ) {
		if ( currentProgram ) {
			qglUseProgram( 0 );
			currentProgram = NULL;
			tr.currentGLSLProgram = NULL;
		}
		return;
	}
	
	if ( currentProgram == program ) {
		return;
	}
	
	qglUseProgram( program->program );
	currentProgram = program;
	tr.currentGLSLProgram = program;
	program->inUse = qtrue;
}

/*
=================
R_UnuseGLSLProgram
=================
*/
void R_UnuseGLSLProgram( void ) {
	if ( currentProgram ) {
		qglUseProgram( 0 );
		currentProgram->inUse = qfalse;
		currentProgram = NULL;
		tr.currentGLSLProgram = NULL;
	}
}

// Forward declaration
static void R_LoadBuiltinShaders( void );

/*
=================
R_InitGLSL
=================
*/
qboolean R_InitGLSL( void ) {
	if ( !r_glsl->integer ) {
		tr.glslAvailable = qfalse;
		return qfalse;
	}
	
	if ( !R_CheckGLSLSupport() ) {
		ri.Printf( PRINT_WARNING, "GLSL not available, falling back to fixed-function pipeline\n" );
		tr.glslAvailable = qfalse;
		return qfalse;
	}
	
	tr.glslAvailable = qtrue;
	numGLSLPrograms = 0;
	currentProgram = NULL;
	
	// Load built-in shaders
	R_LoadBuiltinShaders();
	
	ri.Printf( PRINT_ALL, "GLSL initialized successfully\n" );
	
	return qtrue;
}

/*
=================
R_LoadBuiltinShaders
=================
*/
static void R_LoadBuiltinShaders( void ) {
	char *vertSource, *fragSource;
	char shaderPath[MAX_QPATH];
	
	// World shader
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/world.vert" );
	vertSource = R_LoadShaderSource( shaderPath );
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/world.frag" );
	fragSource = R_LoadShaderSource( shaderPath );
	if ( vertSource && fragSource ) {
		tr.worldShader = R_CreateGLSLProgram( "world", vertSource, fragSource );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	} else {
		ri.Printf( PRINT_WARNING, "Failed to load world shader\n" );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	}
	
	// Tone mapping shader
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/postprocess.vert" );
	vertSource = R_LoadShaderSource( shaderPath );
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/tonemap.frag" );
	fragSource = R_LoadShaderSource( shaderPath );
	if ( vertSource && fragSource ) {
		tr.tonemapShader = R_CreateGLSLProgram( "tonemap", vertSource, fragSource );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	} else {
		ri.Printf( PRINT_WARNING, "Failed to load tonemap shader\n" );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	}
	
	// PBR shader
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/pbr.vert" );
	vertSource = R_LoadShaderSource( shaderPath );
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/pbr.frag" );
	fragSource = R_LoadShaderSource( shaderPath );
	if ( vertSource && fragSource ) {
		tr.pbrShader = R_CreateGLSLProgram( "pbr", vertSource, fragSource );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	} else {
		ri.Printf( PRINT_WARNING, "Failed to load PBR shader\n" );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	}
	
	// Shadow shader
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/shadow.vert" );
	vertSource = R_LoadShaderSource( shaderPath );
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/shadow.frag" );
	fragSource = R_LoadShaderSource( shaderPath );
	if ( vertSource && fragSource ) {
		tr.shadowMapShader = R_CreateGLSLProgram( "shadow", vertSource, fragSource );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	} else {
		ri.Printf( PRINT_WARNING, "Failed to load shadow shader\n" );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	}
	
	// SSAO shader
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/postprocess.vert" );
	vertSource = R_LoadShaderSource( shaderPath );
	Com_sprintf( shaderPath, sizeof(shaderPath), "shaders/ssao.frag" );
	fragSource = R_LoadShaderSource( shaderPath );
	if ( vertSource && fragSource ) {
		tr.ssaoShader = R_CreateGLSLProgram( "ssao", vertSource, fragSource );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	} else {
		ri.Printf( PRINT_WARNING, "Failed to load SSAO shader\n" );
		if ( fragSource ) ri.FS_FreeFile( fragSource );
		if ( vertSource ) ri.FS_FreeFile( vertSource );
	}
}

/*
=================
R_ShutdownGLSL
=================
*/
void R_ShutdownGLSL( void ) {
	int i;
	
	R_UnuseGLSLProgram();
	
	for ( i = 0; i < numGLSLPrograms; i++ ) {
		if ( glslPrograms[i].compiled ) {
			if ( glslPrograms[i].vertexShader ) {
				qglDeleteShader( glslPrograms[i].vertexShader );
			}
			if ( glslPrograms[i].fragmentShader ) {
				qglDeleteShader( glslPrograms[i].fragmentShader );
			}
			if ( glslPrograms[i].program ) {
				qglDeleteProgram( glslPrograms[i].program );
			}
		}
	}
	
	numGLSLPrograms = 0;
	tr.glslAvailable = qfalse;
}

