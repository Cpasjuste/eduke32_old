/**************************************************************************************************
"POLYMOST" code originally written by Ken Silverman
Ken Silverman's official web site: http://www.advsys.net/ken

"POLYMOST2" changes Copyright (c) 2018, Alex Dawson
**************************************************************************************************/


#ifdef USE_OPENGL

#include "build.h"
#include "common.h"
#include "engine_priv.h"
#include "kplib.h"
#include "mdsprite.h"
#include "polymost.h"
#include "tilepacker.h"

extern char textfont[2048], smalltextfont[2048];

int32_t rendmode=0;
int32_t usemodels=1;
int32_t usehightile=1;

typedef struct { float x, cy[2], fy[2]; int32_t tag; int16_t n, p, ctag, ftag; } vsptyp;
#define VSPMAX 2048 //<- careful!
static vsptyp vsp[VSPMAX];
static int32_t gtag, viewportNodeCount;
static float xbl, xbr, xbt, xbb;
int32_t domost_rejectcount;
#ifdef YAX_ENABLE
typedef struct { float x, cy[2]; int32_t tag; int16_t n, p, ctag; } yax_vsptyp;
static yax_vsptyp yax_vsp[YAX_MAXBUNCHES*2][VSPMAX];
typedef struct { float x0, x1, cy[2], fy[2]; } yax_hole_t;
static yax_hole_t yax_holecf[2][VSPMAX];
static int32_t yax_holencf[2];
static int32_t yax_drawcf = -1;
#endif

static float dxb1[MAXWALLSB], dxb2[MAXWALLSB];

//POGOTODO: the SCISDIST could be set to 0 now to allow close objects to render properly,
//          but there's a nasty rendering bug that needs to be dug into when setting SCISDIST lower than 1
#define SCISDIST 1.f  //close plane clipping distance

#define SOFTROTMAT 0

float shadescale = 1.0f;
int32_t shadescale_unbounded = 0;

int32_t r_polymostDebug = 0;
int32_t r_enablepolymost2 = 0;
int32_t r_usenewshading = 4;
int32_t r_usetileshades = 1;
int32_t r_npotwallmode = 2;
int32_t polymostcenterhoriz = 100;

static float gviewxrange;
static float ghoriz, ghoriz2;
static float ghorizcorrect;
double gxyaspect;
float gyxscale, ghalfx, grhalfxdown10, grhalfxdown10x, ghalfy;
float gcosang, gsinang, gcosang2, gsinang2;
float gchang, gshang, gctang, gstang, gvisibility;
float gtang = 0.f;
float gvrcorrection = 1.f;

static vec3d_t xtex, ytex, otex, xtex2, ytex2, otex2;

float fcosglobalang, fsinglobalang;
float fxdim, fydim, fydimen, fviewingrange;

float fsearchx, fsearchy, fsearchz;
int psectnum, pwallnum, pbottomwall, pisbottomwall, psearchstat, doeditorcheck = 0;

static int32_t drawpoly_srepeat = 0, drawpoly_trepeat = 0;
#define MAX_DRAWPOLY_VERTS 8
#define BUFFER_OFFSET(bytes) (GLintptr) ((GLubyte*) NULL + (bytes))
// these cvars are never used directly in rendering -- only when glinit() is called/renderer reset
// We do this because we don't want to accidentally overshoot our existing buffer's bounds
uint32_t r_persistentStreamBuffer = 1;
uint32_t persistentStreamBuffer = r_persistentStreamBuffer;
int32_t r_drawpolyVertsBufferLength = 30000;
int32_t drawpolyVertsBufferLength = r_drawpolyVertsBufferLength;
static GLuint drawpolyVertsID = 0;
static GLint drawpolyVertsOffset = 0;
static int32_t drawpolyVertsSubBufferIndex = 0;
static GLsync drawpolyVertsSync[3] = { 0 };
static float defaultDrawpolyVertsArray[MAX_DRAWPOLY_VERTS*5];
static float* drawpolyVerts = defaultDrawpolyVertsArray;

struct glfiltermodes glfiltermodes[NUMGLFILTERMODES] =
{
    {"GL_NEAREST",GL_NEAREST,GL_NEAREST},
    {"GL_LINEAR",GL_LINEAR,GL_LINEAR},
    {"GL_NEAREST_MIPMAP_NEAREST",GL_NEAREST_MIPMAP_NEAREST,GL_NEAREST},
    {"GL_LINEAR_MIPMAP_NEAREST",GL_LINEAR_MIPMAP_NEAREST,GL_LINEAR},
    {"GL_NEAREST_MIPMAP_LINEAR",GL_NEAREST_MIPMAP_LINEAR,GL_NEAREST},
    {"GL_LINEAR_MIPMAP_LINEAR",GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR}
};

int32_t glanisotropy = 0;            // 0 = maximum supported by card
int32_t gltexfiltermode = TEXFILTER_OFF;

#ifdef EDUKE32_GLES
int32_t glusetexcompr = 2;
int32_t glusetexcache = 0, glusememcache = 0;
#else
int32_t glusetexcompr = 1;
int32_t glusetexcache = 2, glusememcache = 1;
int32_t r_polygonmode = 0;     // 0:GL_FILL,1:GL_LINE,2:GL_POINT //FUK
static int32_t lastglpolygonmode = 0; //FUK
#endif
#ifdef USE_GLEXT
int32_t r_detailmapping = 1;
int32_t r_glowmapping = 1;
#endif

int polymost2d;

int32_t gltexmaxsize = 0;      // 0 means autodetection on first run
int32_t gltexmiplevel = 0;		// discards this many mipmap levels
int32_t glprojectionhacks = 2;
static GLuint polymosttext = 0;
int32_t glrendmode = REND_POLYMOST;
int32_t r_shadeinterpolate = 1;

// This variable, and 'shadeforfullbrightpass' control the drawing of
// fullbright tiles.  Also see 'fullbrightloadingpass'.

int32_t r_fullbrights = 1;
int32_t r_vertexarrays = 1;
#ifdef USE_GLEXT
//POGOTODO: we no longer support rendering without VBOs -- update any outdated pre-GL2 code that renders without VBOs
int32_t r_vbos = 1;
int32_t r_vbocount = 64;
#endif
int32_t r_animsmoothing = 1;
int32_t r_downsize = 0;
int32_t r_downsizevar = -1;

int32_t r_yshearing = 0;
int32_t r_flatsky = 1;

// used for fogcalc
static float fogresult, fogresult2;
coltypef fogcol, fogtable[MAXPALOOKUPS];

static uint32_t currentShaderProgramID = 0;
static GLenum currentActiveTexture = 0;
static uint32_t currentTextureID = 0;

static GLuint quadVertsID = 0;
static GLuint polymost2BasicShaderProgramID = 0;
static GLint texSamplerLoc = -1;
static GLint fullBrightSamplerLoc = -1;
static GLint projMatrixLoc = -1;
static GLint mvMatrixLoc = -1;
static GLint texOffsetLoc = -1;
static GLint texScaleLoc = -1;
static GLint tintLoc = -1;
static GLint alphaLoc = -1;
static GLint fogRangeLoc = -1;
static GLint fogColorLoc = -1;

#define PALSWAP_TEXTURE_SIZE 2048
int32_t r_useindexedcolortextures = -1;
static GLuint tilesheetTexIDs[MAXTILESHEETS];
static GLint tilesheetSize = 0;
static vec2f_t tilesheetHalfTexelSize = { 0.f, 0.f };
static int32_t lastbasepal = -1;
static GLuint paletteTextureIDs[MAXBASEPALS];
static GLuint palswapTextureID = 0;
extern char const *polymost1Frag;
extern char const *polymost1Vert;
static GLuint polymost1CurrentShaderProgramID = 0;
static GLuint polymost1BasicShaderProgramID = 0;
static GLuint polymost1ExtendedShaderProgramID = 0;
static GLint polymost1TexSamplerLoc = -1;
static GLint polymost1PalSwapSamplerLoc = -1;
static GLint polymost1PaletteSamplerLoc = -1;
static GLint polymost1DetailSamplerLoc = -1;
static GLint polymost1GlowSamplerLoc = -1;
static GLint polymost1TexturePosSizeLoc = -1;
static vec4f_t polymost1TexturePosSize = { 0.f, 0.f, 1.f, 1.f };
static GLint polymost1HalfTexelSizeLoc = -1;
static vec2f_t polymost1HalfTexelSize = { 0.f, 0.f };
static GLint polymost1PalswapPosLoc = -1;
static vec2f_t polymost1PalswapPos = { 0.f, 0.f };
static GLint polymost1PalswapSizeLoc = -1;
static vec2f_t polymost1PalswapSize = { 0.f, 0.f };
static vec2f_t polymost1PalswapInnerSize = { 0.f, 0.f };
static GLint polymost1ClampLoc = -1;
static vec2f_t polymost1Clamp = { 0.f, 0.f };
static GLint polymost1ShadeLoc = -1;
static float polymost1Shade = 0.f;
static GLint polymost1NumShadesLoc = -1;
static float polymost1NumShades = 64.f;
static GLint polymost1VisFactorLoc = -1;
static float polymost1VisFactor = 128.f;
static GLint polymost1FogEnabledLoc = -1;
static float polymost1FogEnabled = 1.f;
static GLint polymost1UseColorOnlyLoc = -1;
static float polymost1UseColorOnly = 0.f;
static GLint polymost1UsePaletteLoc = -1;
static float polymost1UsePalette = 1.f;
static GLint polymost1UseDetailMappingLoc = -1;
static float polymost1UseDetailMapping = 0.f;
static GLint polymost1UseGlowMappingLoc = -1;
static float polymost1UseGlowMapping = 0.f;
static GLint polymost1NPOTEmulationLoc = -1;
static float polymost1NPOTEmulation = 0.f;
static GLint polymost1NPOTEmulationFactorLoc = -1;
static float polymost1NPOTEmulationFactor = 1.f;
static GLint polymost1NPOTEmulationXOffsetLoc = -1;
static float polymost1NPOTEmulationXOffset = 0.f;
static GLint polymost1RotMatrixLoc = -1;
static float polymost1RotMatrix[16] = { 1.f, 0.f, 0.f, 0.f,
                                        0.f, 1.f, 0.f, 0.f,
                                        0.f, 0.f, 1.f, 0.f,
                                        0.f, 0.f, 0.f, 1.f };
static GLint polymost1ShadeInterpolateLoc = -1;
static float polymost1ShadeInterpolate = 1.f;

static inline float float_trans(uint32_t maskprops, uint8_t blend)
{
    switch (maskprops)
    {
    case DAMETH_TRANS1:
    case DAMETH_TRANS2:
        return glblend[blend].def[maskprops-2].alpha;
    default:
        return 1.0f;
    }
}

char ptempbuf[MAXWALLSB<<1];

// polymost ART sky control
int32_t r_parallaxskyclamping = 1;
int32_t r_parallaxskypanning = 1;

#define MIN_CACHETIME_PRINT 10

// this was faster in MSVC but slower with GCC... currently unknown on ARM where both
// the FPU and possibly the optimization path in the compiler need improvement
#if 0
static inline int32_t __float_as_int(float f) { return *(int32_t *) &f; }
static inline float __int_as_float(int32_t d) { return *(float *) &d; }
static inline float Bfabsf(float f) { return __int_as_float(__float_as_int(f)&0x7fffffff); }
#else
#define Bfabsf fabsf
#endif

int32_t mdtims, omdtims;
uint8_t alphahackarray[MAXTILES];
int32_t drawingskybox = 0;
int32_t hicprecaching = 0;

hitdata_t polymost_hitdata;

void polymost_outputGLDebugMessage(uint8_t severity, const char* format, ...)
{
    static char msg[8192];
    va_list vArgs;

    if (!glinfo.debugoutput ||
        r_polymostDebug < severity)
    {
        return;
    }

    va_start(vArgs, format);
    Bvsnprintf(msg, sizeof(msg), format, vArgs);
    va_end(vArgs);

    glDebugMessageInsertARB(GL_DEBUG_SOURCE_APPLICATION_ARB,
                            GL_DEBUG_TYPE_OTHER_ARB,
                            0,
                            GL_DEBUG_SEVERITY_HIGH_ARB+severity-1,
                            -1,
                            msg);
}

#if 0
static inline int32_t gltexmayhavealpha(int32_t dapicnum, int32_t dapalnum)
{
    const int32_t j = (dapicnum&(GLTEXCACHEADSIZ-1));
    pthtyp *pth;

    for (pth=texcache.list[j]; pth; pth=pth->next)
        if (pth->picnum == dapicnum && pth->palnum == dapalnum)
            return ((pth->flags&PTH_HASALPHA) != 0);

    return 1;
}
#endif

void gltexinvalidate(int32_t dapicnum, int32_t dapalnum, int32_t dameth)
{
    const int32_t pic = (dapicnum&(GLTEXCACHEADSIZ-1));

    for (pthtyp *pth=texcache.list[pic]; pth; pth=pth->next)
        if (pth->picnum == dapicnum && pth->palnum == dapalnum &&
            (pth->flags & PTH_CLAMPED) == TO_PTH_CLAMPED(dameth))
        {
            pth->flags |= PTH_INVALIDATED;
            if (pth->flags & PTH_HASFULLBRIGHT)
                pth->ofb->flags |= PTH_INVALIDATED;
        }
}

//Make all textures "dirty" so they reload, but not re-allocate
//This should be much faster than polymost_glreset()
//Use this for palette effects ... but not ones that change every frame!
void gltexinvalidatetype(int32_t type)
{
    for (bssize_t j=0; j<=GLTEXCACHEADSIZ-1; j++)
    {
        for (pthtyp *pth=texcache.list[j]; pth; pth=pth->next)
        {
            if (type == INVALIDATE_ALL ||
                (type == INVALIDATE_ALL_NON_INDEXED && !(pth->flags & PTH_INDEXED)) ||
                (type == INVALIDATE_ART && pth->hicr == NULL) ||
                (type == INVALIDATE_ART_NON_INDEXED && pth->hicr == NULL && !(pth->flags & PTH_INDEXED)))
            {
                pth->flags |= PTH_INVALIDATED;
                if (pth->flags & PTH_HASFULLBRIGHT)
                    pth->ofb->flags |= PTH_INVALIDATED;
            }
        }
    }

    clearskins(type);

#ifdef DEBUGGINGAIDS
    OSD_Printf("gltexinvalidateall()\n");
#endif
}

static void bind_2d_texture(GLuint texture, int filter)
{
    if (filter == -1)
        filter = gltexfiltermode;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glfiltermodes[filter].mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glfiltermodes[filter].min);
#ifdef USE_GLEXT
    if (glinfo.maxanisotropy > 1.f)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, glanisotropy);
#endif
}

void gltexapplyprops(void)
{
    if (videoGetRenderMode() == REND_CLASSIC)
        return;

    if (glinfo.maxanisotropy > 1.f)
    {
        if (glanisotropy <= 0 || glanisotropy > glinfo.maxanisotropy)
            glanisotropy = (int32_t)glinfo.maxanisotropy;
    }

    gltexfiltermode = clamp(gltexfiltermode, 0, NUMGLFILTERMODES-1);
    r_useindexedcolortextures = !gltexfiltermode;

    for (bssize_t i=0; i<=GLTEXCACHEADSIZ-1; i++)
    {
        for (pthtyp *pth=texcache.list[i]; pth; pth=pth->next)
        {
            if (pth->flags & PTH_INDEXED)
            {
                //POGO: indexed textures should not be filtered
                continue;
            }

            int32_t const filter = (pth->flags & PTH_FORCEFILTER) ? TEXFILTER_ON : -1;

            bind_2d_texture(pth->glpic, filter);

            if (r_fullbrights && pth->flags & PTH_HASFULLBRIGHT)
                bind_2d_texture(pth->ofb->glpic, filter);
        }
    }

    for (bssize_t i=0; i<nextmodelid; i++)
    {
        md2model_t *m = (md2model_t *)models[i];

        if (m->mdnum < 2)
            continue;

        for (bssize_t j = 0; j < m->numskins * HICTINT_MEMORY_COMBINATIONS; j++)
        {
            if (!m->texid[j])
                continue;
            bind_2d_texture(m->texid[j], -1);
        }

        for (mdskinmap_t *sk = m->skinmap; sk; sk = sk->next)
            for (bssize_t j = 0; j < HICTINT_MEMORY_COMBINATIONS; j++)
            {
                if (!sk->texid[j])
                    continue;
                bind_2d_texture(sk->texid[j], (sk->flags & HICR_FORCEFILTER) ? TEXFILTER_ON : -1);
            }
    }
}

//--------------------------------------------------------------------------------------------------

//Use this for both initialization and uninitialization of OpenGL.
static int32_t gltexcacnum = -1;

//in-place multiply m0=m0*m1
float* multiplyMatrix4f(float m0[4*4], const float m1[4*4])
{
    float mR[4*4];

#define multMatrix4RowCol(r, c) mR[r*4+c] = m0[r*4]*m1[c] + m0[r*4+1]*m1[c+4] + m0[r*4+2]*m1[c+8] + m0[r*4+3]*m1[c+12]

    multMatrix4RowCol(0, 0);
    multMatrix4RowCol(0, 1);
    multMatrix4RowCol(0, 2);
    multMatrix4RowCol(0, 3);

    multMatrix4RowCol(1, 0);
    multMatrix4RowCol(1, 1);
    multMatrix4RowCol(1, 2);
    multMatrix4RowCol(1, 3);

    multMatrix4RowCol(2, 0);
    multMatrix4RowCol(2, 1);
    multMatrix4RowCol(2, 2);
    multMatrix4RowCol(2, 3);

    multMatrix4RowCol(3, 0);
    multMatrix4RowCol(3, 1);
    multMatrix4RowCol(3, 2);
    multMatrix4RowCol(3, 3);

    Bmemcpy(m0, mR, sizeof(float)*4*4);

    return m0;

#undef multMatrix4RowCol
}

static void calcmat(vec3f_t a0, const vec2f_t *offset, float f, float mat[16], int16_t angle)
{
    float g;
    float k0, k1, k2, k3, k4, k5, k6, k7;

    k0 = a0.y;
    k1 = a0.x;
    a0.x += offset->x;
    a0.z += offset->y;
    f = gcosang2*gshang;
    g = gsinang2*gshang;
    k4 = (float)sintable[(angle+1024)&2047] * (1.f/16384.f);
    k5 = (float)sintable[(angle+512)&2047] * (1.f/16384.f);
    k2 = k0*(1-k4)+k1*k5;
    k3 = k1*(1-k4)-k0*k5;
    k6 = f*gstang - gsinang*gctang; k7 = g*gstang + gcosang*gctang;
    mat[0] = k4*k6 + k5*k7; mat[4] = gchang*gstang; mat[ 8] = k4*k7 - k5*k6; mat[12] = k2*k6 + k3*k7;
    k6 = f*gctang + gsinang*gstang; k7 = g*gctang - gcosang*gstang;
    mat[1] = k4*k6 + k5*k7; mat[5] = gchang*gctang; mat[ 9] = k4*k7 - k5*k6; mat[13] = k2*k6 + k3*k7;
    k6 =           gcosang2*gchang; k7 =           gsinang2*gchang;
    mat[2] = k4*k6 + k5*k7; mat[6] =-gshang;        mat[10] = k4*k7 - k5*k6; mat[14] = k2*k6 + k3*k7;

    mat[12] = (mat[12] + a0.y*mat[0]) + (a0.z*mat[4] + a0.x*mat[ 8]);
    mat[13] = (mat[13] + a0.y*mat[1]) + (a0.z*mat[5] + a0.x*mat[ 9]);
    mat[14] = (mat[14] + a0.y*mat[2]) + (a0.z*mat[6] + a0.x*mat[10]);
}

static GLuint polymost2_compileShader(GLenum shaderType, const char* const source, int * pLength = nullptr)
{
    GLuint shaderID = glCreateShader(shaderType);
    if (shaderID == 0)
    {
        return 0;
    }

    glShaderSource(shaderID,
                   1,
                   &source,
                   pLength);
    glCompileShader(shaderID);

    GLint compileStatus;
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &compileStatus);
    if (!compileStatus)
    {
        GLint logLength;
        glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &logLength);
        OSD_Printf("Compile Status: %u\n", compileStatus);
        if (logLength > 0)
        {
            char *infoLog = (char*)Xmalloc(logLength);
            glGetShaderInfoLog(shaderID, logLength, &logLength, infoLog);
            OSD_Printf("Log:\n%s\n", infoLog);
            free(infoLog);
        }
    }

    return shaderID;
}

static GLuint polymost2_compileShader(GLenum shaderType, const char* const source, int length)
{
    return polymost2_compileShader(shaderType, source, &length);
}

void polymost_glreset()
{
    for (bssize_t i=0; i<=MAXPALOOKUPS-1; i++)
    {
        fogtable[i].r = palookupfog[i].r * (1.f/255.f);
        fogtable[i].g = palookupfog[i].g * (1.f/255.f);
        fogtable[i].b = palookupfog[i].b * (1.f/255.f);
        fogtable[i].a = 0;
    }

    //Reset if this is -1 (meaning 1st texture call ever), or > 0 (textures in memory)
    if (gltexcacnum < 0)
    {
        gltexcacnum = 0;

        //Hack for polymost_dorotatesprite calls before 1st polymost_drawrooms()
        gcosang = gcosang2 = 16384.f/262144.f;
        gsinang = gsinang2 = 0.f;
    }
    else
    {
        for (bssize_t i = 0; i <= GLTEXCACHEADSIZ-1; i++)
        {
            for (pthtyp *pth = texcache.list[i]; pth;)
            {
                pthtyp *const next = pth->next;

                if (pth->flags & PTH_HASFULLBRIGHT)
                {
                    glDeleteTextures(1, &pth->ofb->glpic);
                    Xfree(pth->ofb);
                }

                glDeleteTextures(1, &pth->glpic);
                Xfree(pth);
                pth = next;
            }

            texcache.list[i] = NULL;
        }

        clearskins(INVALIDATE_ALL);
    }

    if (polymosttext)
        glDeleteTextures(1,&polymosttext);
    polymosttext=0;

#ifdef USE_GLEXT
    md_freevbos();
#endif

    Bmemset(texcache.list,0,sizeof(texcache.list));

    texcache_freeptrs();
    texcache_syncmemcache();

#ifdef DEBUGGINGAIDS
    OSD_Printf("polymost_glreset()\n");
#endif
}

#if defined EDUKE32_GLES
static void Polymost_DetermineTextureFormatSupport(void);
#endif

// reset vertex pointers to polymost default
void polymost_resetVertexPointers()
{
    polymost_outputGLDebugMessage(3, "polymost_resetVertexPointers()");

    glBindBuffer(GL_ARRAY_BUFFER, drawpolyVertsID);

    glVertexPointer(3, GL_FLOAT, 5*sizeof(float), 0);
    glTexCoordPointer(2, GL_FLOAT, 5*sizeof(float), (GLvoid*) (3*sizeof(float)));

#ifdef USE_GLEXT
    if (r_detailmapping)
    {
        glClientActiveTexture(GL_TEXTURE3);
        glTexCoordPointer(2, GL_FLOAT, 5*sizeof(float), (GLvoid*) (3*sizeof(float)));
    }
    if (r_glowmapping)
    {
        glClientActiveTexture(GL_TEXTURE4);
        glTexCoordPointer(2, GL_FLOAT, 5*sizeof(float), (GLvoid*) (3*sizeof(float)));
    }
    glClientActiveTexture(GL_TEXTURE0);
#endif

    polymost_resetProgram();
}

void polymost_disableProgram()
{
    if (videoGetRenderMode() != REND_POLYMOST)
        return;

    polymost_outputGLDebugMessage(3, "polymost_disableProgram()");

    polymost_useShaderProgram(0);
}

void polymost_resetProgram()
{
    if (videoGetRenderMode() != REND_POLYMOST)
        return;

    polymost_outputGLDebugMessage(3, "polymost_resetProgram()");

    if (r_enablepolymost2)
        polymost_useShaderProgram(polymost2BasicShaderProgramID);
    else
        polymost_useShaderProgram(polymost1CurrentShaderProgramID);

    // ensure that palswapTexture and paletteTexture[curbasepal] is bound
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, palswapTextureID);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, paletteTextureIDs[curbasepal]);
    glActiveTexture(GL_TEXTURE0);
}

static void polymost_setCurrentShaderProgram(uint32_t programID)
{
    polymost_outputGLDebugMessage(3, "polymost_setCurrentShaderProgram(programID:%u)", programID);

    polymost1CurrentShaderProgramID = programID;
    polymost_useShaderProgram(programID);

    //update the uniform locations
    polymost1TexSamplerLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "s_texture");
    polymost1PalSwapSamplerLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "s_palswap");
    polymost1PaletteSamplerLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "s_palette");
    polymost1DetailSamplerLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "s_detail");
    polymost1GlowSamplerLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "s_glow");
    polymost1TexturePosSizeLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_texturePosSize");
    polymost1HalfTexelSizeLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_halfTexelSize");
    polymost1PalswapPosLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_palswapPos");
    polymost1PalswapSizeLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_palswapSize");
    polymost1ClampLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_clamp");
    polymost1ShadeLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_shade");
    polymost1NumShadesLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_numShades");
    polymost1VisFactorLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_visFactor");
    polymost1FogEnabledLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_fogEnabled");
    polymost1UsePaletteLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_usePalette");
    polymost1UseColorOnlyLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_useColorOnly");
    polymost1UseDetailMappingLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_useDetailMapping");
    polymost1UseGlowMappingLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_useGlowMapping");
    polymost1NPOTEmulationLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_npotEmulation");
    polymost1NPOTEmulationFactorLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_npotEmulationFactor");
    polymost1NPOTEmulationXOffsetLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_npotEmulationXOffset");
    polymost1RotMatrixLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_rotMatrix");
    polymost1ShadeInterpolateLoc = glGetUniformLocation(polymost1CurrentShaderProgramID, "u_shadeInterpolate");

    //set the uniforms to the current values
    glUniform4f(polymost1TexturePosSizeLoc, polymost1TexturePosSize.x, polymost1TexturePosSize.y, polymost1TexturePosSize.z, polymost1TexturePosSize.w);
    glUniform2f(polymost1HalfTexelSizeLoc, polymost1HalfTexelSize.x, polymost1HalfTexelSize.y);
    glUniform2f(polymost1PalswapPosLoc, polymost1PalswapPos.x, polymost1PalswapPos.y);
    glUniform2f(polymost1PalswapSizeLoc, polymost1PalswapInnerSize.x, polymost1PalswapInnerSize.y);
    glUniform2f(polymost1ClampLoc, polymost1Clamp.x, polymost1Clamp.y);
    glUniform1f(polymost1ShadeLoc, polymost1Shade);
    glUniform1f(polymost1NumShadesLoc, polymost1NumShades);
    glUniform1f(polymost1VisFactorLoc, polymost1VisFactor);
    glUniform1f(polymost1FogEnabledLoc, polymost1FogEnabled);
    glUniform1f(polymost1UseColorOnlyLoc, polymost1UseColorOnly);
    glUniform1f(polymost1UsePaletteLoc, polymost1UsePalette);
    glUniform1f(polymost1UseDetailMappingLoc, polymost1UseDetailMapping);
    glUniform1f(polymost1UseGlowMappingLoc, polymost1UseGlowMapping);
    glUniform1f(polymost1NPOTEmulationLoc, polymost1NPOTEmulation);
    glUniform1f(polymost1NPOTEmulationFactorLoc, polymost1NPOTEmulationFactor);
    glUniform1f(polymost1NPOTEmulationXOffsetLoc, polymost1NPOTEmulationXOffset);
    glUniformMatrix4fv(polymost1RotMatrixLoc, 1, false, polymost1RotMatrix);
    glUniform1f(polymost1ShadeInterpolateLoc, polymost1ShadeInterpolate);
}

void polymost_setTexturePosSize(vec4f_t const &texturePosSize)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID)
        return;

    polymost1TexturePosSize = texturePosSize;
    glUniform4f(polymost1TexturePosSizeLoc, polymost1TexturePosSize.x, polymost1TexturePosSize.y, polymost1TexturePosSize.z, polymost1TexturePosSize.w);
}

void polymost_setHalfTexelSize(vec2f_t const &halfTexelSize)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || (halfTexelSize.x == polymost1HalfTexelSize.x && halfTexelSize.y == polymost1HalfTexelSize.y))
        return;

    polymost1HalfTexelSize = halfTexelSize;
    glUniform2f(polymost1HalfTexelSizeLoc, polymost1HalfTexelSize.x, polymost1HalfTexelSize.y);
}

static void polymost_setPalswap(uint32_t index)
{
    static uint32_t lastPalswapIndex;

    if (currentShaderProgramID != polymost1CurrentShaderProgramID || index == lastPalswapIndex)
        return;

    lastPalswapIndex = index;
    polymost1PalswapPos.x = index*polymost1PalswapSize.x;
    polymost1PalswapPos.y = floorf(polymost1PalswapPos.x);
    polymost1PalswapPos = { polymost1PalswapPos.x - polymost1PalswapPos.y + (0.5f/PALSWAP_TEXTURE_SIZE),
                            polymost1PalswapPos.y * polymost1PalswapSize.y + (0.5f/PALSWAP_TEXTURE_SIZE) };
    glUniform2f(polymost1PalswapPosLoc, polymost1PalswapPos.x, polymost1PalswapPos.y);
}

static void polymost_setPalswapSize(uint32_t width, uint32_t height)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID)
        return;

    polymost1PalswapSize = { width*(1.f/PALSWAP_TEXTURE_SIZE),
                             height*(1.f/PALSWAP_TEXTURE_SIZE) };

    polymost1PalswapInnerSize = { (width-1)*(1.f/PALSWAP_TEXTURE_SIZE),
                                  (height-1)*(1.f/PALSWAP_TEXTURE_SIZE) };

    glUniform2f(polymost1PalswapSizeLoc, polymost1PalswapInnerSize.x, polymost1PalswapInnerSize.y);
}

char polymost_getClamp()
{
    return polymost1Clamp.x + polymost1Clamp.y*2.0;
}

void polymost_setClamp(char clamp)
{
    char clampx = clamp&1;
    char clampy = clamp>>1;
    if (currentShaderProgramID != polymost1CurrentShaderProgramID ||
        (clampx == polymost1Clamp.x && clampy == polymost1Clamp.y))
        return;

    polymost1Clamp.x = clampx;
    polymost1Clamp.y = clampy;
    glUniform2f(polymost1ClampLoc, polymost1Clamp.x, polymost1Clamp.y);
}

static void polymost_setShade(int32_t shade)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID)
        return;

    if (!polymost_usetileshades())
        shade = 0;

    static int32_t lastShade;
    static int32_t lastNumShades;

    if (shade != lastShade)
    {
        lastShade = shade;
        polymost1Shade = shade;
        glUniform1f(polymost1ShadeLoc, polymost1Shade);
    }

    if (numshades != lastNumShades)
    {
        lastNumShades = numshades;
        polymost1NumShades = numshades;
        glUniform1f(polymost1NumShadesLoc, polymost1NumShades);
    }
}

void polymost_setVisibility(float visibility)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID)
        return;

    if (!polymost_usetileshades())
        visibility = -16;

    float visFactor = visibility * fviewingrange * (1.f / (64.f * 65536.f));
    if (visFactor == polymost1VisFactor)
        return;

    polymost1VisFactor = visFactor;
    glUniform1f(polymost1VisFactorLoc, polymost1VisFactor);
}

void polymost_setFogEnabled(char fogEnabled)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || fogEnabled == polymost1FogEnabled)
        return;

    polymost1FogEnabled = fogEnabled;
    glUniform1f(polymost1FogEnabledLoc, polymost1FogEnabled);
}

void polymost_useColorOnly(char useColorOnly)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || useColorOnly == polymost1UseColorOnly)
        return;

    polymost1UseColorOnly = useColorOnly;
    glUniform1f(polymost1UseColorOnlyLoc, polymost1UseColorOnly);
}

void polymost_usePaletteIndexing(char usePaletteIndexing)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || usePaletteIndexing == polymost1UsePalette)
        return;

    polymost1UsePalette = usePaletteIndexing;
    glUniform1f(polymost1UsePaletteLoc, polymost1UsePalette);
}

void polymost_useDetailMapping(char useDetailMapping)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || useDetailMapping == polymost1UseDetailMapping)
        return;

    if (useDetailMapping)
        polymost_setCurrentShaderProgram(polymost1ExtendedShaderProgramID);

    polymost1UseDetailMapping = useDetailMapping;
    glUniform1f(polymost1UseDetailMappingLoc, polymost1UseDetailMapping);
}

void polymost_useGlowMapping(char useGlowMapping)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || useGlowMapping == polymost1UseGlowMapping)
        return;

    if (useGlowMapping)
        polymost_setCurrentShaderProgram(polymost1ExtendedShaderProgramID);

    polymost1UseGlowMapping = useGlowMapping;
    glUniform1f(polymost1UseGlowMappingLoc, polymost1UseGlowMapping);
}

void polymost_npotEmulation(char npotEmulation, float factor, float xOffset)
{
    if (currentShaderProgramID != polymost1CurrentShaderProgramID || npotEmulation == polymost1NPOTEmulation)
        return;

    polymost1NPOTEmulation = npotEmulation;
    glUniform1f(polymost1NPOTEmulationLoc, polymost1NPOTEmulation);
    polymost1NPOTEmulationFactor = factor;
    glUniform1f(polymost1NPOTEmulationFactorLoc, polymost1NPOTEmulationFactor);
    polymost1NPOTEmulationXOffset = xOffset;
    glUniform1f(polymost1NPOTEmulationXOffsetLoc, polymost1NPOTEmulationXOffset);
}

void polymost_shadeInterpolate(int32_t shadeInterpolate)
{
    if (currentShaderProgramID == polymost1CurrentShaderProgramID)
    {
        polymost1ShadeInterpolate = shadeInterpolate;
        glUniform1f(polymost1ShadeInterpolateLoc, polymost1ShadeInterpolate);
    }
}

void polymost_activeTexture(GLenum texture)
{
    currentActiveTexture = texture;
    glad_glActiveTexture(texture);
}

//POGOTODO: replace this and polymost_activeTexture with proper draw call organization
void polymost_bindTexture(GLenum target, uint32_t textureID)
{
    if (currentTextureID != textureID ||
        textureID == 0 ||
        currentActiveTexture != GL_TEXTURE0 ||
        videoGetRenderMode() != REND_POLYMOST)
    {
        glad_glBindTexture(target, textureID);
        if (currentActiveTexture == GL_TEXTURE0)
        {
            currentTextureID = textureID;
        }
    }
}

static void polymost_bindPth(pthtyp const * const pPth)
{
    Bassert(pPth);

    vec4f_t texturePosSize = { 0.f, 0.f, 1.f, 1.f };
    vec2f_t halfTexelSize = { 0.f, 0.f };
    if ((pPth->flags & PTH_INDEXED) &&
        !(pPth->flags & PTH_HIGHTILE))
    {
        Tile tile;
        char tileIsPacked = tilepacker_getTile(waloff[pPth->picnum] ? pPth->picnum+1 : 0, &tile);
        //POGO: check the width and height to ensure that the tile hasn't been changed for a user tile that has different dimensions
        if (tileIsPacked &&
            (!waloff[pPth->picnum] ||
             (tile.rect.width >= (uint32_t) tilesiz[pPth->picnum].y &&
              tile.rect.height >= (uint32_t) tilesiz[pPth->picnum].x)))
        {
            texturePosSize = { tile.rect.u/(float) tilesheetSize,
                               tile.rect.v/(float) tilesheetSize,
                               tilesiz[pPth->picnum].y/(float) tilesheetSize,
                               tilesiz[pPth->picnum].x/(float) tilesheetSize };
            halfTexelSize = tilesheetHalfTexelSize;
        }
    }
    polymost_setTexturePosSize(texturePosSize);
    polymost_setHalfTexelSize(halfTexelSize);
    glBindTexture(GL_TEXTURE_2D, pPth->glpic);
}

void polymost_useShaderProgram(uint32_t shaderID)
{
    glUseProgram(shaderID);
    currentShaderProgramID = shaderID;
}

// one-time initialization of OpenGL for polymost
void polymost_glinit()
{
    glHint(GL_FOG_HINT, GL_NICEST);
    glFogi(GL_FOG_MODE, (r_usenewshading < 2) ? GL_EXP2 : GL_LINEAR);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (glinfo.depthclamp)
        glEnable(GL_DEPTH_CLAMP);

    //glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    //glEnable(GL_LINE_SMOOTH);

    if (r_useindexedcolortextures == -1)
    {
        //POGO: r_useindexedcolortextures has never been set, so force it to be enabled
        gltexfiltermode = 0;
    }

#ifdef USE_GLEXT
    if (r_persistentStreamBuffer && ((!glinfo.bufferstorage) || (!glinfo.sync)))
    {
        OSD_Printf("Your OpenGL implementation doesn't support the required extensions for persistent stream buffers. Disabling...\n");
        r_persistentStreamBuffer = 0;
    }
#endif

    //POGOTODO: require a max texture size >= 2048

    persistentStreamBuffer = r_persistentStreamBuffer;
    drawpolyVertsBufferLength = r_drawpolyVertsBufferLength;

    drawpolyVertsOffset = 0;
    drawpolyVertsSubBufferIndex = 0;

    GLuint ids[2];
    glGenBuffers(2, ids);
    drawpolyVertsID = ids[0];
    glBindBuffer(GL_ARRAY_BUFFER, drawpolyVertsID);
    if (persistentStreamBuffer)
    {
        // reset the sync objects, as old ones we had from any last GL context are gone now
        Bmemset(drawpolyVertsSync, 0, sizeof(drawpolyVertsSync));

        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        // we want to triple-buffer to avoid having to wait for the buffer to become available again,
        // so triple the buffer size we expect to use
        glBufferStorage(GL_ARRAY_BUFFER, 3*drawpolyVertsBufferLength*sizeof(float)*5, NULL, flags);
        drawpolyVerts = (float*) glMapBufferRange(GL_ARRAY_BUFFER, 0, 3*drawpolyVertsBufferLength*sizeof(float)*5, flags);
    }
    else
    {
        drawpolyVerts = defaultDrawpolyVertsArray;
        glBufferData(GL_ARRAY_BUFFER, drawpolyVertsBufferLength*sizeof(float)*5, NULL, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    currentTextureID = 0;

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tilesheetSize);
#if (defined _MSC_VER) || (!defined BITNESS64)
    if (tilesheetSize > 8192)
        tilesheetSize = 8192;
#endif
    tilesheetHalfTexelSize = { 0.5f/tilesheetSize, 0.5f/tilesheetSize };
    vec2_t maxTexDimensions = { tilesheetSize, tilesheetSize };
    char allPacked = false;
    static int numTilesheets = 0;
    //POGO: only pack the tilesheets once
    if (numTilesheets == 0)
    {
        // add a blank texture for tileUID 0
        tilepacker_addTile(0, 2, 2);
        for (int picnum = 0; picnum < MAXTILES; ++picnum)
        {
            tilepacker_addTile(picnum+1, (uint32_t) tilesiz[picnum].y, (uint32_t) tilesiz[picnum].x);
        }

        do
        {
            tilepacker_initTilesheet(numTilesheets, tilesheetSize, tilesheetSize);
            allPacked = tilepacker_pack(numTilesheets);
            ++numTilesheets;
        } while (!allPacked && numTilesheets < MAXTILESHEETS);
    }
    for (int i = 0; i < numTilesheets; ++i)
    {
        glGenTextures(1, tilesheetTexIDs+i);
        glBindTexture(GL_TEXTURE_2D, tilesheetTexIDs[i]);
        uploadtextureindexed(true, {0, 0}, maxTexDimensions, (intptr_t) NULL);
    }

    const char blankTex[] = {255, 255,
                             255, 255};
    Tile blankTile;
    tilepacker_getTile(0, &blankTile);
    glBindTexture(GL_TEXTURE_2D, tilesheetTexIDs[blankTile.tilesheetID]);
    uploadtextureindexed(false, {(int32_t) blankTile.rect.u, (int32_t) blankTile.rect.v}, {2, 2}, (intptr_t) blankTex);

    quadVertsID = ids[1];
    glBindBuffer(GL_ARRAY_BUFFER, quadVertsID);
    const float quadVerts[] =
        {
            -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, //top-left
            -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, //bottom-left
             0.5f, 1.0f, 0.0f, 1.0f, 1.0f, //top-right
             0.5f, 0.0f, 0.0f, 1.0f, 0.0f  //bottom-right
        };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

    //specify format/arrangement for vertex positions:
    glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(float) * 5, 0);
    //specify format/arrangement for vertex texture coords:
    glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(float) * 5, (const void*) (sizeof(float) * 3));

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const char* const POLYMOST2_BASIC_VERTEX_SHADER_CODE =
        "#version 110\n\
         \n\
         // input\n\
         attribute vec3 i_vertPos;\n\
         attribute vec2 i_texCoord;\n\
         uniform mat4 u_mvMatrix;\n\
         uniform mat4 u_projMatrix;\n\
         uniform vec2 u_texOffset;\n\
         uniform vec2 u_texScale;\n\
         \n\
         // output\n\
         varying vec2 v_texCoord;\n\
         varying float v_distance;\n\
         \n\
         void main()\n\
         {\n\
            vec4 eyeCoordPosition = u_mvMatrix * vec4(i_vertPos, 1.0);\n\
            gl_Position = u_projMatrix * eyeCoordPosition;\n\
            \n\
            eyeCoordPosition.xyz /= eyeCoordPosition.w;\n\
            \n\
            v_texCoord = i_texCoord * u_texScale + u_texOffset;\n\
            v_distance = eyeCoordPosition.z;\n\
         }\n";
    const char* const POLYMOST2_BASIC_FRAGMENT_SHADER_CODE =
        "#version 110\n\
         \n\
         varying vec2 v_texCoord;\n\
         uniform sampler2D s_texture;\n\
         uniform sampler2D s_fullBright;\n\
         \n\
         uniform vec4 u_tint;\n\
         uniform float u_alpha;\n\
         \n\
         varying float v_distance;\n\
         uniform vec2 u_fogRange;\n\
         uniform vec4 u_fogColor;\n\
         \n\
         const float c_zero = 0.0;\n\
         const float c_one  = 1.0;\n\
         \n\
         void main()\n\
         {\n\
             vec4 color = texture2D(s_texture, v_texCoord);\n\
             vec4 fullBrightColor = texture2D(s_fullBright, v_texCoord);\n\
             \n\
             float fogFactor = clamp((u_fogRange.y-v_distance)/(u_fogRange.y-u_fogRange.x), c_zero, c_one);\n\
             \n\
             color.rgb = mix(u_fogColor.rgb, color.rgb, fogFactor);\n\
             color.rgb *= u_tint.rgb * u_tint.a * color.a;\n\
             color.rgb = mix(color.rgb, fullBrightColor.rgb, fullBrightColor.a);\n\
             \n\
             color.a *= u_alpha;\n\
             \n\
             gl_FragColor = color;\n\
         }\n";

    polymost2BasicShaderProgramID = glCreateProgram();
    GLuint polymost2BasicVertexShaderID = polymost2_compileShader(GL_VERTEX_SHADER, POLYMOST2_BASIC_VERTEX_SHADER_CODE);
    GLuint polymost2BasicFragmentShaderID = polymost2_compileShader(GL_FRAGMENT_SHADER, POLYMOST2_BASIC_FRAGMENT_SHADER_CODE);
    glBindAttribLocation(polymost2BasicShaderProgramID, 0, "i_vertPos");
    glBindAttribLocation(polymost2BasicShaderProgramID, 1, "i_texCoord");
    glAttachShader(polymost2BasicShaderProgramID, polymost2BasicVertexShaderID);
    glAttachShader(polymost2BasicShaderProgramID, polymost2BasicFragmentShaderID);
    glLinkProgram(polymost2BasicShaderProgramID);

    // Get the attribute/uniform locations
    texSamplerLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "s_texture");
    fullBrightSamplerLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "s_fullBright");
    projMatrixLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_projMatrix");
    mvMatrixLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_mvMatrix");
    texOffsetLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_texOffset");
    texScaleLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_texScale");
    tintLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_tint");
    alphaLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_alpha");
    fogRangeLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_fogRange");
    fogColorLoc = glGetUniformLocation(polymost2BasicShaderProgramID, "u_fogColor");

    polymost1ExtendedShaderProgramID = glCreateProgram();
    GLuint polymost1BasicVertexShaderID = polymost2_compileShader(GL_VERTEX_SHADER, polymost1Vert);
    GLuint polymost1ExtendedFragmentShaderID = polymost2_compileShader(GL_FRAGMENT_SHADER, polymost1Frag);
    glAttachShader(polymost1ExtendedShaderProgramID, polymost1BasicVertexShaderID);
    glAttachShader(polymost1ExtendedShaderProgramID, polymost1ExtendedFragmentShaderID);
    glLinkProgram(polymost1ExtendedShaderProgramID);

    int polymost1BasicFragLen = strlen(polymost1Frag);
    char* polymost1BasicFrag = (char*) malloc(polymost1BasicFragLen);
    memcpy(polymost1BasicFrag, polymost1Frag, polymost1BasicFragLen);
    char* extDefineSubstr = strstr(polymost1BasicFrag, " #define POLYMOST1_EXTENDED");
    if (extDefineSubstr)
    {
        //Disable extensions for basic fragment shader
        extDefineSubstr[0] = '/';
        extDefineSubstr[1] = '/';
    }
    polymost1BasicShaderProgramID = glCreateProgram();
    GLuint polymost1BasicFragmentShaderID = polymost2_compileShader(GL_FRAGMENT_SHADER, polymost1BasicFrag, polymost1BasicFragLen);
    glAttachShader(polymost1BasicShaderProgramID, polymost1BasicVertexShaderID);
    glAttachShader(polymost1BasicShaderProgramID, polymost1BasicFragmentShaderID);
    glLinkProgram(polymost1BasicShaderProgramID);
    free(polymost1BasicFrag);
    polymost1BasicFrag = 0;

    // set defaults
    polymost_setCurrentShaderProgram(polymost1ExtendedShaderProgramID);
    glUniform1i(polymost1TexSamplerLoc, 0);
    glUniform1i(polymost1PalSwapSamplerLoc, 1);
    glUniform1i(polymost1PaletteSamplerLoc, 2);
    glUniform1i(polymost1DetailSamplerLoc, 3);
    glUniform1i(polymost1GlowSamplerLoc, 4);
    polymost_setPalswapSize(256, numshades+1);
    polymost_setCurrentShaderProgram(polymost1BasicShaderProgramID);
    glUniform1i(polymost1TexSamplerLoc, 0);
    glUniform1i(polymost1PalSwapSamplerLoc, 1);
    glUniform1i(polymost1PaletteSamplerLoc, 2);
    polymost_useShaderProgram(0);

    lastbasepal = -1;
    for (int basepalnum = 0; basepalnum < MAXBASEPALS; ++basepalnum)
    {
        paletteTextureIDs[basepalnum] = 0;
        uploadbasepalette(basepalnum);
    }
    palswapTextureID = 0;
    for (int palookupnum = 0; palookupnum < MAXPALOOKUPS; ++palookupnum)
    {
        uploadpalswap(palookupnum);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    polymost_resetVertexPointers();

    texcache_init();
    texcache_loadoffsets();
    texcache_openfiles();

    texcache_setupmemcache();
    texcache_checkgarbage();

#if defined EDUKE32_GLES
    Polymost_DetermineTextureFormatSupport();
#endif
}

void polymost_init()
{
    lastbasepal = -1;
    polymost_resetVertexPointers();
}

////////// VISIBILITY FOG ROUTINES //////////

// only for r_usenewshading < 2 (not preferred)
static void fogcalc_old(int32_t shade, int32_t vis)
{
    float f;

    if (r_usenewshading == 1)
    {
        f = 0.9f * shade;
        f = (vis > 239) ? (float)(gvisibility * (vis - 240 + f)) :
                          (float)(gvisibility * (vis + 16 + f));
    }
    else
    {
        f = (shade < 0) ? shade * 3.5f : shade * .66f;
        f = (vis > 239) ? (float)(gvisibility * ((vis - 240 + f) / (klabs(vis - 256)))) :
                          (float)(gvisibility * (vis + 16 + f));
    }

    fogresult = clamp(f, 0.001f, 100.0f);
}

// For GL_LINEAR fog:
#define FOGDISTCONST 600
#define FULLVIS_BEGIN 2.9e30f
#define FULLVIS_END 3.0e30f

static inline void fogcalc(int32_t shade, int32_t vis, int32_t pal)
{
    fogcol = fogtable[pal];

    if (r_usenewshading < 2)
    {
        fogcalc_old(shade, vis);
        return;
    }

    float combvis = (float) globalvisibility * (uint8_t) (vis+16);

    if (combvis == 0.f)
    {
        if (r_usenewshading == 2 && shade > 0)
        {
            // beg = -D*shade, end = D*(NUMSHADES-1-shade)
            //  => end/beg = -(NUMSHADES-1-shade)/shade
            fogresult = -FULLVIS_BEGIN;
            fogresult2 = FULLVIS_BEGIN * (float)(numshades-1-shade) / shade;
        }
        else
        {
            fogresult  = FULLVIS_BEGIN;
            fogresult2 = FULLVIS_END;
        }
    }
    else if (r_usenewshading == 3 && shade >= numshades-1)
    {
        fogresult = -1;
        fogresult2 = 0;
    }
    else
    {
        combvis = 1.f/combvis;
        fogresult = (r_usenewshading == 3 && shade > 0) ? 0.f : -(FOGDISTCONST * shade) * combvis;
        fogresult2 = (FOGDISTCONST * (numshades-1-shade)) * combvis;
    }
}

#define GL_FOG_MAX 1.0e37f

void polymost2_calc_fog(int32_t shade, int32_t vis, int32_t pal)
{
    if (nofog) return;

    fogcol = fogtable[pal];

    if (((uint8_t)(vis + 16)) > 0 && g_visibility > 0)
    {
        constexpr GLfloat glfogconstant = 262144.f;
        GLfloat fogrange = (frealmaxshade * glfogconstant) / (((uint8_t)(vis + 16)) * globalvisibility);

        fogresult = 0.f - (((min(shade, 0) - 0.5f) / frealmaxshade) * fogrange); // min() = subtract shades from fog
        fogresult2 = fogrange - (((shade - 0.5f) / frealmaxshade) * fogrange);
    }
    else
    {
        fogresult = 0.f;
        fogresult2 = -GL_FOG_MAX; // hide fog behind the camera
    }
}

void calc_and_apply_fog(int32_t shade, int32_t vis, int32_t pal)
{
    if (nofog) return;

    if (r_usenewshading == 4)
    {
        fogresult = 0.f;
        fogcol = fogtable[pal];

        if (((uint8_t)(vis + 16)) > 0 && globalvisibility > 0)
        {
            constexpr GLfloat glfogconstant = 262144.f;
            GLfloat fogrange = (frealmaxshade * glfogconstant) / (((uint8_t)(vis + 16)) * globalvisibility);

            fogresult = 0.f - (((min(shade, 0) - 0.5f) / frealmaxshade) * fogrange); // min() = subtract shades from fog
            fogresult2 = fogrange - (((shade - 0.5f) / frealmaxshade) * fogrange);
        }
        else
        {
            fogresult = 0.f;
            fogresult2 = -GL_FOG_MAX; // hide fog behind the camera
        }

        glFogf(GL_FOG_START, fogresult);
        glFogf(GL_FOG_END, fogresult2);
        glFogfv(GL_FOG_COLOR, (GLfloat *)&fogcol);

        return;
    }

    fogcalc(shade, vis, pal);
    glFogfv(GL_FOG_COLOR, (GLfloat *)&fogcol);

    if (r_usenewshading < 2)
        glFogf(GL_FOG_DENSITY, fogresult);
    else
    {
        glFogf(GL_FOG_START, fogresult);
        glFogf(GL_FOG_END, fogresult2);
    }
}

void calc_and_apply_fog_factor(int32_t shade, int32_t vis, int32_t pal, float factor)
{
    if (nofog) return;

    if (r_usenewshading == 4)
    {
        fogcol = fogtable[pal];

        if (((uint8_t)(vis + 16)) > 0 && ((((uint8_t)(vis + 16)) / 8.f) + shade) > 0)
        {
            GLfloat normalizedshade = (shade - 0.5f) / frealmaxshade;
            GLfloat fogrange = (((uint8_t)(vis + 16)) / (8.f * frealmaxshade)) + normalizedshade;

            // subtract shades from fog
            if (normalizedshade > 0.f && normalizedshade < 1.f)
                fogrange = (fogrange - normalizedshade) / (1.f - normalizedshade);

            fogresult = -(GL_FOG_MAX * fogrange);
            fogresult2 = GL_FOG_MAX - (GL_FOG_MAX * fogrange);
        }
        else
        {
            fogresult = 0.f;
            fogresult2 = -GL_FOG_MAX; // hide fog behind the camera
        }

        glFogf(GL_FOG_START, fogresult);
        glFogf(GL_FOG_END, fogresult2);
        glFogfv(GL_FOG_COLOR, (GLfloat *)&fogcol);

        return;
    }

    // NOTE: for r_usenewshading >= 2, the fog beginning/ending distance results are
    // unused.
    fogcalc(shade, vis, pal);
    glFogfv(GL_FOG_COLOR, (GLfloat *)&fogcol);

    if (r_usenewshading < 2)
        glFogf(GL_FOG_DENSITY, fogresult*factor);
    else
    {
        glFogf(GL_FOG_START, (GLfloat) FULLVIS_BEGIN);
        glFogf(GL_FOG_END, (GLfloat) FULLVIS_END);
    }
}
////////////////////


static float get_projhack_ratio(void)
{
    if ((glprojectionhacks == 1) && !r_yshearing)
    {
        // calculates the extend of the zenith glitch
        float verticalfovtan = (fviewingrange * (windowxy2.y-windowxy1.y) * 5.f) / ((float)yxaspect * (windowxy2.x-windowxy1.x) * 4.f);
        float verticalfov = atanf(verticalfovtan) * (2.f / fPI);
        static constexpr float const maxhorizangle = 0.6361136f; // horiz of 199 in degrees
        float zenglitch = verticalfov + maxhorizangle - 0.95f; // less than 1 because the zenith glitch extends a bit
        if (zenglitch <= 0.f)
            return 1.f;
        float const zenglitchtan = tanf((verticalfov - zenglitch) * (fPI / 2.f));
        static constexpr float const maxcoshoriz = 0.54097117f; // 128/sqrt(128^2+199^2) = cos of an horiz diff of 199
        return 1.f + (((verticalfovtan / zenglitchtan) - 1.f) * ((1.f - Bfabsf(gchang)) / maxcoshoriz ));
    }

    // No projection hacks (legacy or new-aspect)
    return 1.f;
}

static void resizeglcheck(void)
{
#ifndef EDUKE32_GLES
    //FUK
    if (lastglpolygonmode != r_polygonmode)
    {
        lastglpolygonmode = r_polygonmode;
        switch (r_polygonmode)
        {
        default:
        case 0:
            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); break;
        case 1:
            glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); break;
        case 2:
            glPolygonMode(GL_FRONT_AND_BACK,GL_POINT); break;
        }
    }
    if (r_polygonmode) //FUK
    {
        glClearColor(1.0,1.0,1.0,0.0);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    }
#else
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
#endif

    const int32_t ourxdimen = (windowxy2.x-windowxy1.x+1);
    float ratio = get_projhack_ratio();
    const int32_t fovcorrect = (int32_t)(ourxdimen*ratio - ourxdimen);

    ratio = 1.f/ratio;

    polymost2d = 0;

    glViewport(windowxy1.x-(fovcorrect/2), ydim-(windowxy2.y+1),
                ourxdimen+fovcorrect, windowxy2.y-windowxy1.y+1);

    glMatrixMode(GL_PROJECTION);

    float m[4][4];
    Bmemset(m,0,sizeof(m));

    float const nearclip = 4.f / (gxyaspect * gyxscale * 1024.f);
    float const farclip = 64.f;

    m[0][0] = 1.f;
    m[1][1] = fxdimen / (fydimen * ratio);
    m[2][0] = 2.f * ghoriz2 * gstang / fxdimen;
    m[2][1] = 2.f * (ghoriz2 * gctang + ghorizcorrect) / fydimen;
    m[2][2] = (farclip + nearclip) / (farclip - nearclip);
    m[2][3] = 1.f;
    m[3][2] = -(2.f * farclip * nearclip) / (farclip - nearclip);
    glLoadMatrixf(&m[0][0]);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (!nofog) polymost_setFogEnabled(true);
}

static void fixtransparency(coltype *dapic, vec2_t dasiz, vec2_t dasiz2, int32_t dameth)
{
    if (!(dameth & DAMETH_MASKPROPS))
        return;

    vec2_t doxy = { dasiz2.x-1, dasiz2.y-1 };

    if (dameth & DAMETH_CLAMPED)
        doxy = { min(doxy.x, dasiz.x), min(doxy.y, dasiz.y) };
    else  dasiz = dasiz2; //Make repeating textures duplicate top/left parts

    dasiz.x--; dasiz.y--; //Hacks for optimization inside loop
    int32_t const naxsiz2 = -dasiz2.x;

    //Set transparent pixels to average color of neighboring opaque pixels
    //Doing this makes bilinear filtering look much better for masked textures (I.E. sprites)
    for (bssize_t y=doxy.y; y>=0; y--)
    {
        coltype * wpptr = &dapic[y*dasiz2.x+doxy.x];

        for (bssize_t x=doxy.x; x>=0; x--,wpptr--)
        {
            if (wpptr->a) continue;

            int r = 0, g = 0, b = 0, j = 0;

            if ((x>     0) && (wpptr[     -1].a)) { r += wpptr[     -1].r; g += wpptr[     -1].g; b += wpptr[     -1].b; j++; }
            if ((x<dasiz.x) && (wpptr[     +1].a)) { r += wpptr[     +1].r; g += wpptr[     +1].g; b += wpptr[     +1].b; j++; }
            if ((y>     0) && (wpptr[naxsiz2].a)) { r += wpptr[naxsiz2].r; g += wpptr[naxsiz2].g; b += wpptr[naxsiz2].b; j++; }
            if ((y<dasiz.y) && (wpptr[dasiz2.x].a)) { r += wpptr[dasiz2.x].r; g += wpptr[dasiz2.x].g; b += wpptr[dasiz2.x].b; j++; }

            switch (j)
            {
            case 1:
                wpptr->r =   r            ; wpptr->g =   g            ; wpptr->b =   b            ; break;
            case 2:
                wpptr->r = ((r   +  1)>>1); wpptr->g = ((g   +  1)>>1); wpptr->b = ((b   +  1)>>1); break;
            case 3:
                wpptr->r = ((r*85+128)>>8); wpptr->g = ((g*85+128)>>8); wpptr->b = ((b*85+128)>>8); break;
            case 4:
                wpptr->r = ((r   +  2)>>2); wpptr->g = ((g   +  2)>>2); wpptr->b = ((b   +  2)>>2); break;
            }
        }
    }
}

#if defined EDUKE32_GLES
// sorted first in increasing order of size, then in decreasing order of quality
static int32_t const texfmts_rgb_mask[] = { GL_RGB5_A1, GL_RGBA, 0 };
static int32_t const texfmts_rgb[] = { GL_RGB565, GL_RGB5_A1, GL_RGB, GL_RGBA, 0 };
static int32_t const texfmts_rgba[] = { GL_RGBA4, GL_RGBA, 0 } ;

static int32_t texfmt_rgb_mask;
static int32_t texfmt_rgb;
static int32_t texfmt_rgba;

#if defined EDUKE32_IOS
static int32_t const comprtexfmts_rgb[] = { GL_ETC1_RGB8_OES, 0 };
static int32_t const comprtexfmts_rgba[] = { 0 };
static int32_t const comprtexfmts_rgb_mask[] = { 0 };
#else
static int32_t const comprtexfmts_rgb[] =
{
#ifdef GL_COMPRESSED_RGB8_ETC2
    GL_COMPRESSED_RGB8_ETC2,
#endif
#ifdef GL_ETC1_RGB8_OES
    GL_ETC1_RGB8_OES,
#endif
    0
    };
// TODO: waiting on etcpak support for ETC2 with alpha
static int32_t const comprtexfmts_rgba[] = { /*GL_COMPRESSED_RGBA8_ETC2_EAC,*/ 0 };
static int32_t const comprtexfmts_rgb_mask[] = { /*GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,*/ 0 };
#endif

static int32_t comprtexfmt_rgb_mask;
static int32_t comprtexfmt_rgb;
static int32_t comprtexfmt_rgba;

# ifdef __cplusplus
extern "C" {
# endif
extern uint64_t ProcessRGB(uint8_t const *);
extern uint64_t ProcessRGB_ETC2(uint8_t const *);
# ifdef __cplusplus
}
# endif

typedef uint64_t (*ETCFunction_t)(uint8_t const *);

static ETCFunction_t Polymost_PickETCFunction(int32_t const comprtexfmt)
{
    switch (comprtexfmt)
    {
# ifdef GL_ETC1_RGB8_OES
        case GL_ETC1_RGB8_OES:
            return ProcessRGB;
# endif

# ifdef GL_COMPRESSED_RGB8_ETC2
        case GL_COMPRESSED_RGB8_ETC2:
            return ProcessRGB_ETC2;
# endif

# if 0
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
            fallthrough__;
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            fallthrough__;
# endif

        default:
            EDUKE32_UNREACHABLE_SECTION(return NULL);
    }
}

static int Polymost_ConfirmNoGLError(void)
{
    GLenum checkerr, err = GL_NO_ERROR;
    while ((checkerr = glGetError()) != GL_NO_ERROR)
        err = checkerr;

    return err == GL_NO_ERROR;
}

static int32_t Polymost_TryDummyTexture(coltype const * const pic, int32_t const * formats)
{
    while (*formats)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, *formats, 4,4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pic);

        if (Polymost_ConfirmNoGLError())
            return *formats;

        ++formats;
    }

    initputs("No texture formats supported?!\n");

    return 0;
}

static int32_t Polymost_TryCompressedDummyTexture(coltype const * const pic, int32_t const * formats)
{
    while (*formats)
    {
        ETCFunction_t func = Polymost_PickETCFunction(*formats);
        uint64_t const comprpic = func((uint8_t const *)pic);
        jwzgles_glCompressedTexImage2D(GL_TEXTURE_2D, 0, *formats, 4,4, 0, sizeof(uint64_t), &comprpic);

        if (Polymost_ConfirmNoGLError())
            return *formats;

        ++formats;
    }

    return 0;
}

static void Polymost_DetermineTextureFormatSupport(void)
{
    // init dummy texture to trigger possible failure of all compression modes
    coltype pic[4*4] = { { 0, 0, 0, 0 } };
    GLuint tex = 0;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    BuildGLErrorCheck(); // XXX: Clear errors.

    texfmt_rgb = Polymost_TryDummyTexture(pic, texfmts_rgb);
    texfmt_rgba = Polymost_TryDummyTexture(pic, texfmts_rgba);
    texfmt_rgb_mask = Polymost_TryDummyTexture(pic, texfmts_rgb_mask);

    comprtexfmt_rgb = Polymost_TryCompressedDummyTexture(pic, comprtexfmts_rgb);
    comprtexfmt_rgba = Polymost_TryCompressedDummyTexture(pic, comprtexfmts_rgba);
    comprtexfmt_rgb_mask = Polymost_TryCompressedDummyTexture(pic, comprtexfmts_rgb_mask);

    glDeleteTextures(1, &tex);
}
#endif

static void Polymost_SendTexToDriver(int32_t const doalloc,
                                     vec2_t const siz,
                                     int32_t const texfmt,
                                     coltype const * const pic,
                                     int32_t const intexfmt,
#if defined EDUKE32_GLES
                                     int32_t const comprtexfmt,
                                     int32_t const texcompress_ok,
#endif
                                     int32_t const level)
{
#if defined EDUKE32_GLES
    if (texcompress_ok && comprtexfmt && (siz.x & 3) == 0 && (siz.y & 3) == 0)
    {
        size_t const picLength = siz.x * siz.y;
        size_t const fourRows = siz.x << 2u;
        GLsizei const imageSize = picLength >> 1u; // 4x4 pixels --> 8 bytes
        uint8_t * const comprpic = (uint8_t *)Xaligned_alloc(8, imageSize);

        ETCFunction_t func = Polymost_PickETCFunction(comprtexfmt);

        coltype buf[4*4];
        uint64_t * out = (uint64_t *)comprpic;
        for (coltype const * row = pic, * const pic_end = pic + picLength; row < pic_end; row += fourRows)
            for (coltype const * block = row, * const row_end = row + siz.x; block < row_end; block += 4)
            {
                buf[0] = block[0];
                buf[1] = block[siz.x];
                buf[2] = block[siz.x*2];
                buf[3] = block[siz.x*3];
                buf[4] = block[1];
                buf[5] = block[siz.x+1];
                buf[6] = block[siz.x*2+1];
                buf[7] = block[siz.x*3+1];
                buf[8] = block[2];
                buf[9] = block[siz.x+2];
                buf[10] = block[siz.x*2+2];
                buf[11] = block[siz.x*3+2];
                buf[12] = block[3];
                buf[13] = block[siz.x+3];
                buf[14] = block[siz.x*2+3];
                buf[15] = block[siz.x*3+3];

                *out++ = func((uint8_t const *)buf);
            }

        if (doalloc & 1)
            jwzgles_glCompressedTexImage2D(GL_TEXTURE_2D, level, comprtexfmt, siz.x,siz.y, 0, imageSize, comprpic);
        else
            jwzgles_glCompressedTexSubImage2D(GL_TEXTURE_2D, level, 0,0, siz.x,siz.y, comprtexfmt, imageSize, comprpic);

        Xaligned_free(comprpic);

        return;
    }
#endif

#if B_BIG_ENDIAN
    GLenum type = GL_UNSIGNED_INT_8_8_8_8;
#else
    GLenum type = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
    if (doalloc & 1)
        glTexImage2D(GL_TEXTURE_2D, level, intexfmt, siz.x,siz.y, 0, texfmt, type, pic);
    else
        glTexSubImage2D(GL_TEXTURE_2D, level, 0,0, siz.x,siz.y, texfmt, type, pic);
}

void uploadtexture(int32_t doalloc, vec2_t siz, int32_t texfmt,
                   coltype *pic, vec2_t tsiz, int32_t dameth)
{
    const int artimmunity = !!(dameth & DAMETH_ARTIMMUNITY);
    const int hi = !!(dameth & DAMETH_HI);
    const int nodownsize = !!(dameth & DAMETH_NODOWNSIZE) || artimmunity;
    const int nomiptransfix  = !!(dameth & DAMETH_NOFIX);
    const int texcompress_ok = !(dameth & DAMETH_NOTEXCOMPRESS) && (glusetexcompr == 2 || (glusetexcompr && !artimmunity));

#if !defined EDUKE32_GLES
    int32_t intexfmt;
    if (texcompress_ok && glinfo.texcompr)
        intexfmt = GL_COMPRESSED_RGBA;
    else
        intexfmt = GL_RGBA8;
#else
    const int hasalpha  = !!(dameth & (DAMETH_HASALPHA|DAMETH_ONEBITALPHA));
    const int onebitalpha  = !!(dameth & DAMETH_ONEBITALPHA);

    int32_t const intexfmt = hasalpha ? (onebitalpha ? texfmt_rgb_mask : texfmt_rgba) : texfmt_rgb;
    int32_t const comprtexfmt = hasalpha ? (onebitalpha ? comprtexfmt_rgb_mask : comprtexfmt_rgba) : comprtexfmt_rgb;
#endif

    dameth &= ~DAMETH_UPLOADTEXTURE_MASK;

    if (gltexmaxsize <= 0)
    {
        GLint i = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &i);
        if (!i) gltexmaxsize = 6;   // 2^6 = 64 == default GL max texture size
        else
        {
            gltexmaxsize = 0;
            for (; i>1; i>>=1) gltexmaxsize++;
#ifdef EDUKE32_GLES
            while ((1<<(gltexmaxsize-1)) > xdim)
                gltexmaxsize--;
#endif
        }
    }

    gltexmiplevel = max(0, min(gltexmaxsize-1, gltexmiplevel));

    int miplevel = gltexmiplevel;

    while ((siz.x >> miplevel) > (1 << gltexmaxsize) || (siz.y >> miplevel) > (1 << gltexmaxsize))
        miplevel++;

    if (hi && !nodownsize && r_downsize > miplevel)
        miplevel = r_downsize;

    // don't use mipmaps if mipmapping is disabled
    //POGO: until the texcacheheader can be updated, generate the mipmaps texcache expects if it's enabled
    if (!glusetexcache &&
        (glfiltermodes[gltexfiltermode].min == GL_NEAREST ||
         glfiltermodes[gltexfiltermode].min == GL_LINEAR))
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }

    if (!miplevel)
        Polymost_SendTexToDriver(doalloc, siz, texfmt, pic,
                                 intexfmt,
#if defined EDUKE32_GLES
                                 comprtexfmt,
                                 texcompress_ok,
#endif
                                 0);

    // don't generate mipmaps if we're not going to use them
    if (!glusetexcache &&
        (glfiltermodes[gltexfiltermode].min == GL_NEAREST ||
         glfiltermodes[gltexfiltermode].min == GL_LINEAR))
    {
        return;
    }

    vec2_t siz2 = siz;

    for (bssize_t j=1; (siz2.x > 1) || (siz2.y > 1); j++)
    {
        vec2_t const siz3 = { max(1, siz2.x >> 1), max(1, siz2.y >> 1) };  // this came from the GL_ARB_texture_non_power_of_two spec
        //x3 = ((x2+1)>>1); y3 = ((y2+1)>>1);

        for (bssize_t y=0; y<siz3.y; y++)
        {
            coltype *wpptr = &pic[y*siz3.x];
            coltype const *rpptr = &pic[(y<<1)*siz2.x];

            for (bssize_t x=0; x<siz3.x; x++,wpptr++,rpptr+=2)
            {
                int32_t r=0, g=0, b=0, a=0, k=0;

                if (rpptr[0].a)                  { r += rpptr[0].r; g += rpptr[0].g; b += rpptr[0].b; a += rpptr[0].a; k++; }
                if ((x+x+1 < siz2.x) && (rpptr[1].a)) { r += rpptr[1].r; g += rpptr[1].g; b += rpptr[1].b; a += rpptr[1].a; k++; }
                if (y+y+1 < siz2.y)
                {
                    if ((rpptr[siz2.x].a)) { r += rpptr[siz2.x  ].r; g += rpptr[siz2.x  ].g; b += rpptr[siz2.x  ].b; a += rpptr[siz2.x  ].a; k++; }
                    if ((x+x+1 < siz2.x) && (rpptr[siz2.x+1].a)) { r += rpptr[siz2.x+1].r; g += rpptr[siz2.x+1].g; b += rpptr[siz2.x+1].b; a += rpptr[siz2.x+1].a; k++; }
                }
                switch (k)
                {
                case 0:
                case 1:
                    wpptr->r = r; wpptr->g = g; wpptr->b = b; wpptr->a = a; break;
                case 2:
                    wpptr->r = ((r+1)>>1); wpptr->g = ((g+1)>>1); wpptr->b = ((b+1)>>1); wpptr->a = ((a+1)>>1); break;
                case 3:
                    wpptr->r = ((r*85+128)>>8); wpptr->g = ((g*85+128)>>8); wpptr->b = ((b*85+128)>>8); wpptr->a = ((a*85+128)>>8); break;
                case 4:
                    wpptr->r = ((r+2)>>2); wpptr->g = ((g+2)>>2); wpptr->b = ((b+2)>>2); wpptr->a = ((a+2)>>2); break;
                default:
                    EDUKE32_UNREACHABLE_SECTION(break);
                }
                //if (wpptr->a) wpptr->a = 255;
            }
        }

        if (!nomiptransfix)
        {
            vec2_t const tsizzle = { (tsiz.x + (1 << j)-1) >> j, (tsiz.y + (1 << j)-1) >> j };

            fixtransparency(pic, tsizzle, siz3, dameth);
        }

        if (j >= miplevel)
            Polymost_SendTexToDriver(doalloc, siz3, texfmt, pic,
                                     intexfmt,
#if defined EDUKE32_GLES
                                     comprtexfmt,
                                     texcompress_ok,
#endif
                                     j - miplevel);

        siz2 = siz3;
    }
}

void uploadtextureindexed(int32_t doalloc, vec2_t offset, vec2_t siz, intptr_t tile)
{
    if (doalloc & 1)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, siz.y, siz.x, 0, GL_RED, GL_UNSIGNED_BYTE, (void*) tile);
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, offset.x, offset.y, siz.y, siz.x, GL_RED, GL_UNSIGNED_BYTE, (void*) tile);
    }
}

void uploadbasepalette(int32_t basepalnum)
{
    if (!polymost1BasicShaderProgramID)
    {
        //POGO: if we haven't initialized properly yet, we shouldn't be uploading base palettes
        return;
    }
    if (!basepaltable[basepalnum])
    {
        return;
    }

    //POGO: this is only necessary for GL fog/vertex color shade compatibility, since those features don't index into shade tables
    uint8_t basepalWFullBrightInfo[4*256];
    for (int i = 0; i < 256; ++i)
    {
        basepalWFullBrightInfo[i*4] = basepaltable[basepalnum][i*3];
        basepalWFullBrightInfo[i*4+1] = basepaltable[basepalnum][i*3+1];
        basepalWFullBrightInfo[i*4+2] = basepaltable[basepalnum][i*3+2];
        basepalWFullBrightInfo[i*4+3] = 0-(IsPaletteIndexFullbright(i) != 0);
    }

    char allocateTexture = !paletteTextureIDs[basepalnum];
    if (allocateTexture)
    {
        glGenTextures(1, &paletteTextureIDs[basepalnum]);
    }
    glBindTexture(GL_TEXTURE_2D, paletteTextureIDs[basepalnum]);
    if (allocateTexture)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, basepalWFullBrightInfo);
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, basepalWFullBrightInfo);
    }
}

void uploadpalswap(int32_t palookupnum)
{
    if (!polymost1BasicShaderProgramID)
    {
        //POGO: if we haven't initialized properly yet, we shouldn't be uploading palette swap tables
        return;
    }
    if (!palookup[palookupnum])
    {
        return;
    }

    char allocateTexture = !palswapTextureID;
    if (allocateTexture)
    {
        glGenTextures(1, &palswapTextureID);
    }
    glBindTexture(GL_TEXTURE_2D, palswapTextureID);
    if (allocateTexture)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, PALSWAP_TEXTURE_SIZE, PALSWAP_TEXTURE_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    }

    int32_t column = palookupnum%(PALSWAP_TEXTURE_SIZE/256);
    int32_t row = palookupnum/(PALSWAP_TEXTURE_SIZE/256);
    int32_t rowOffset = (numshades+1)*row;
    if (rowOffset > PALSWAP_TEXTURE_SIZE)
    {
        OSD_Printf("Polymost: palswaps are too large for palswap tilesheet!\n");
        return;
    }
    //POGO: There was a reason why having an extra row of black pixels was necessary along the edge of the palswap (I believe it affected a particular IHV/GPU).
    //      It may be worth investigating what this reason was again, but for now, make sure we properly initialize this row.
    static char blackPixels256[256] = {0};
    glTexSubImage2D(GL_TEXTURE_2D, 0, 256*column, rowOffset+numshades, 256, 1, GL_RED, GL_UNSIGNED_BYTE, blackPixels256);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 256*column, rowOffset, 256, numshades, GL_RED, GL_UNSIGNED_BYTE, palookup[palookupnum]);
}


#if 0
// TODO: make configurable
static int32_t tile_is_sky(int32_t tilenum)
{
    return return (tilenum >= 78 /*CLOUDYOCEAN*/ && tilenum <= 99 /*REDSKY2*/);
}
# define clamp_if_tile_is_sky(x, y) (tile_is_sky(x) ? (y) : GL_REPEAT)
#else
# define clamp_if_tile_is_sky(x, y) (GL_REPEAT)
#endif

static void polymost_setuptexture(const int32_t dameth, int filter)
{
    const GLuint clamp_mode = glinfo.clamptoedge ? GL_CLAMP_TO_EDGE : GL_CLAMP;

    gltexfiltermode = clamp(gltexfiltermode, 0, NUMGLFILTERMODES-1);

    if (filter == -1)
        filter = gltexfiltermode;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glfiltermodes[filter].mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glfiltermodes[filter].min);

#ifdef USE_GLEXT
    if (glinfo.maxanisotropy > 1.f)
    {
        uint32_t i = (unsigned)Blrintf(glinfo.maxanisotropy);

        if ((unsigned)glanisotropy > i)
            glanisotropy = i;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, glanisotropy);
    }
#endif

    if (!(dameth & DAMETH_CLAMPED))
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_if_tile_is_sky(dapic, clamp_mode));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    else
    {
        // For sprite textures, clamping looks better than wrapping
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_mode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp_mode);
    }
}

static void gloadtile_art_indexed(int32_t dapic, int32_t dameth, pthtyp *pth, int32_t doalloc)
{
    vec2_16_t const & tsizart = tilesiz[dapic];
    vec2_t siz = { tsizart.x, tsizart.y };
    //POGOTODO: npoty
    char npoty = 0;

    //POGOTODO: if !glinfo.texnpot, then we could allocate a texture of the pow2 size, and then populate the subportion using buffersubdata func
    //if (!glinfo.texnpot)

    Tile tile = {};
    if (waloff[dapic])
    {
        char tileIsPacked = tilepacker_getTile(dapic+1, &tile);
        if (tileIsPacked)
        {
            if (tile.rect.width >= (uint32_t) tsizart.y &&
                tile.rect.height >= (uint32_t) tsizart.x)
            {
                if (pth->glpic != 0 &&
                    pth->glpic != tilesheetTexIDs[tile.tilesheetID])
                {
                    //POGO: we have a separate texture for this tile, but we want to merge it back into the tilesheet
                    glDeleteTextures(1, &pth->glpic);
                }
                pth->glpic = tilesheetTexIDs[tile.tilesheetID];
                doalloc = false;
            } else if (pth->glpic == tilesheetTexIDs[tile.tilesheetID])
            {
                //POGO: we're reloading an invalidated art tile that has changed dimensions and no longer fits into our original tilesheet
                doalloc = true;
            }
        } else
        {
            Tile blankTile = {};
            tilepacker_getTile(0, &blankTile);
            if (pth->glpic == tilesheetTexIDs[blankTile.tilesheetID])
            {
                //POGO: we're reloading an invalidated art tile that had previously been added to the texcache while !waloff[dapic]
                doalloc = true;
            }
        }

        if (doalloc)
        {
            glGenTextures(1, (GLuint *)&pth->glpic);
        }
        glBindTexture(GL_TEXTURE_2D, pth->glpic);

        if (doalloc)
        {
            const GLuint clamp_mode = glinfo.clamptoedge ? GL_CLAMP_TO_EDGE : GL_CLAMP;
            if (!(dameth & DAMETH_CLAMPED))
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_if_tile_is_sky(dapic, clamp_mode));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            }
            else
            {
                // For sprite textures, clamping looks better than wrapping
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_mode);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp_mode);
            }
        }

        if (!doalloc &&
            !tileIsPacked &&
            (siz.x != pth->siz.x ||
             siz.y != pth->siz.y))
        {
            //POGO: resize our texture to match the tile data
            doalloc = true;
        }
        uploadtextureindexed(doalloc, {(int32_t) tile.rect.u, (int32_t) tile.rect.v}, siz, waloff[dapic]);
    }
    else
    {
        tilepacker_getTile(0, &tile);
        pth->glpic = tilesheetTexIDs[tile.tilesheetID];
    }

    pth->picnum = dapic;
    pth->palnum = 0;
    pth->shade = 0;
    pth->effects = 0;
    pth->flags = TO_PTH_CLAMPED(dameth) | TO_PTH_NOTRANSFIX(dameth) | (PTH_HASALPHA|PTH_ONEBITALPHA) | (npoty*PTH_NPOTWALL) | PTH_INDEXED;
    pth->hicr = NULL;
    pth->siz = siz;
}

void gloadtile_art(int32_t dapic, int32_t dapal, int32_t tintpalnum, int32_t dashade, int32_t dameth, pthtyp *pth, int32_t doalloc)
{
    if (dameth & DAMETH_INDEXED)
    {
        return gloadtile_art_indexed(dapic, dameth, pth, doalloc);
    }

    static int32_t fullbrightloadingpass = 0;
    vec2_16_t const & tsizart = tilesiz[dapic];
    vec2_t siz = { 0, 0 }, tsiz = { tsizart.x, tsizart.y };
    int const picdim = tsiz.x*tsiz.y;
    char hasalpha = 0, hasfullbright = 0;
    char npoty = 0;

    texcacheheader cachead;
    char texcacheid[BMAX_PATH];
    {
        // Absolutely disgusting.
        uint32_t firstint = 0;
        if (waloff[dapic])
            Bmemcpy(&firstint, (void *)waloff[dapic], min(4, picdim));
        sprintf(texcacheid, "%08x", firstint);
    }
    texcache_calcid(texcacheid, texcacheid, picdim | ((unsigned)dapal<<24u), DAMETH_NARROW_MASKPROPS(dameth) | ((unsigned)dapic<<8u) | ((unsigned)dashade<<24u), tintpalnum);
    int32_t gotcache = texcache_readtexheader(texcacheid, &cachead, 0);

    if (gotcache && !texcache_loadtile(&cachead, &doalloc, pth))
    {
        hasalpha = !!(cachead.flags & CACHEAD_HASALPHA);
        hasfullbright = !!(cachead.flags & CACHEAD_HASFULLBRIGHT);
        npoty = !!(cachead.flags & CACHEAD_NPOTWALL);
    }
    else
    {
        if (!glinfo.texnpot)
        {
            for (siz.x = 1; siz.x < tsiz.x; siz.x += siz.x) { }
            for (siz.y = 1; siz.y < tsiz.y; siz.y += siz.y) { }
        }
        else
        {
            if ((tsiz.x|tsiz.y) == 0)
                siz.x = siz.y = 1;
            else
                siz = tsiz;
        }

        coltype *pic = (coltype *)Xmalloc(siz.x*siz.y*sizeof(coltype));

        if (!waloff[dapic])
        {
            //Force invalid textures to draw something - an almost purely transparency texture
            //This allows the Z-buffer to be updated for mirrors (which are invalidated textures)
            pic[0].r = pic[0].g = pic[0].b = 0; pic[0].a = 1;
            tsiz.x = tsiz.y = 1; hasalpha = 1;
        }
        else
        {
            const int dofullbright = !(picanm[dapic].sf & PICANM_NOFULLBRIGHT_BIT) && !(globalflags & GLOBAL_NO_GL_FULLBRIGHT);

            for (bssize_t y = 0; y < siz.y; y++)
            {
                coltype *wpptr = &pic[y * siz.x];
                int32_t y2 = (y < tsiz.y) ? y : y - tsiz.y;

                for (bssize_t x = 0; x < siz.x; x++, wpptr++)
                {
                    int32_t dacol;
                    int32_t x2 = (x < tsiz.x) ? x : x-tsiz.x;

                    if ((dameth & DAMETH_CLAMPED) && (x >= tsiz.x || y >= tsiz.y)) //Clamp texture
                    {
                        wpptr->r = wpptr->g = wpptr->b = wpptr->a = 0;
                        continue;
                    }

                    dacol = *(char *)(waloff[dapic]+x2*tsiz.y+y2);

                    if (dacol == 255)
                    {
                        wpptr->a = 0;
                        hasalpha = 1;
                    }
                    else
                        wpptr->a = 255;

                    char *p = (char *)(palookup[dapal])+(int32_t)(dashade<<8);
                    dacol = (uint8_t)p[dacol];

                    if (!fullbrightloadingpass)
                    {
                        // regular texture
                        if (IsPaletteIndexFullbright(dacol) && dofullbright)
                            hasfullbright = 1;
                    }
                    else
                    {
                        // texture with only fullbright areas
                        if (!IsPaletteIndexFullbright(dacol))    // regular colors
                        {
                            wpptr->a = 0;
                            hasalpha = 1;
                        }
                    }

                    bricolor((palette_t *)wpptr, dacol);

                    if (!fullbrightloadingpass && tintpalnum >= 0)
                    {
                        polytint_t const & tint = hictinting[tintpalnum];
                        polytintflags_t const effect = tint.f;
                        uint8_t const r = tint.r;
                        uint8_t const g = tint.g;
                        uint8_t const b = tint.b;

                        if (effect & HICTINT_GRAYSCALE)
                        {
                            wpptr->g = wpptr->r = wpptr->b = (uint8_t) ((wpptr->r * GRAYSCALE_COEFF_RED) +
                                                                  (wpptr->g * GRAYSCALE_COEFF_GREEN) +
                                                                  (wpptr->b * GRAYSCALE_COEFF_BLUE));
                        }

                        if (effect & HICTINT_INVERT)
                        {
                            wpptr->b = 255 - wpptr->b;
                            wpptr->g = 255 - wpptr->g;
                            wpptr->r = 255 - wpptr->r;
                        }

                        if (effect & HICTINT_COLORIZE)
                        {
                            wpptr->b = min((int32_t)((wpptr->b) * b) >> 6, 255);
                            wpptr->g = min((int32_t)((wpptr->g) * g) >> 6, 255);
                            wpptr->r = min((int32_t)((wpptr->r) * r) >> 6, 255);
                        }

                        switch (effect & HICTINT_BLENDMASK)
                        {
                            case HICTINT_BLEND_SCREEN:
                                wpptr->b = 255 - (((255 - wpptr->b) * (255 - b)) >> 8);
                                wpptr->g = 255 - (((255 - wpptr->g) * (255 - g)) >> 8);
                                wpptr->r = 255 - (((255 - wpptr->r) * (255 - r)) >> 8);
                                break;
                            case HICTINT_BLEND_OVERLAY:
                                wpptr->b = wpptr->b < 128 ? (wpptr->b * b) >> 7 : 255 - (((255 - wpptr->b) * (255 - b)) >> 7);
                                wpptr->g = wpptr->g < 128 ? (wpptr->g * g) >> 7 : 255 - (((255 - wpptr->g) * (255 - g)) >> 7);
                                wpptr->r = wpptr->r < 128 ? (wpptr->r * r) >> 7 : 255 - (((255 - wpptr->r) * (255 - r)) >> 7);
                                break;
                            case HICTINT_BLEND_HARDLIGHT:
                                wpptr->b = b < 128 ? (wpptr->b * b) >> 7 : 255 - (((255 - wpptr->b) * (255 - b)) >> 7);
                                wpptr->g = g < 128 ? (wpptr->g * g) >> 7 : 255 - (((255 - wpptr->g) * (255 - g)) >> 7);
                                wpptr->r = r < 128 ? (wpptr->r * r) >> 7 : 255 - (((255 - wpptr->r) * (255 - r)) >> 7);
                                break;
                        }
                    }

                    //swap r & b so that we deal with the data as BGRA
                    uint8_t tmpR = wpptr->r;
                    wpptr->r = wpptr->b;
                    wpptr->b = tmpR;
                }
            }
        }

        if (doalloc) glGenTextures(1,(GLuint *)&pth->glpic); //# of textures (make OpenGL allocate structure)
        glBindTexture(GL_TEXTURE_2D, pth->glpic);

        fixtransparency(pic,tsiz,siz,dameth);

        if (polymost_want_npotytex(dameth, siz.y) && tsiz.x == siz.x && tsiz.y == siz.y)  // XXX
        {
            const int32_t nextpoty = 1 << ((picsiz[dapic] >> 4) + 1);
            const int32_t ydif = nextpoty - siz.y;
            coltype *paddedpic;

            Bassert(ydif < siz.y);

            paddedpic = (coltype *)Xrealloc(pic, siz.x * nextpoty * sizeof(coltype));

            pic = paddedpic;
            Bmemcpy(&pic[siz.x * siz.y], pic, siz.x * ydif * sizeof(coltype));
            siz.y = tsiz.y = nextpoty;

            npoty = 1;
        }

        if (!doalloc)
        {
            vec2_t pthSiz2 = pth->siz;
            if (!glinfo.texnpot)
            {
                for (pthSiz2.x = 1; pthSiz2.x < pth->siz.x; pthSiz2.x += pthSiz2.x) { }
                for (pthSiz2.y = 1; pthSiz2.y < pth->siz.y; pthSiz2.y += pthSiz2.y) { }
            }
            else
            {
                if ((pthSiz2.x|pthSiz2.y) == 0)
                    pthSiz2.x = pthSiz2.y = 1;
                else
                    pthSiz2 = pth->siz;
            }
            if (siz.x > pthSiz2.x ||
                siz.y > pthSiz2.y)
            {
                //POGO: grow our texture to hold the tile data
                doalloc = true;
            }
        }
        uploadtexture(doalloc, siz, GL_BGRA, pic, tsiz,
                      dameth | DAMETH_ARTIMMUNITY |
                      (dapic >= MAXUSERTILES ? (DAMETH_NOTEXCOMPRESS|DAMETH_NODOWNSIZE) : 0) | /* never process these short-lived tiles */
                      (hasfullbright ? DAMETH_HASFULLBRIGHT : 0) |
                      (npoty ? DAMETH_NPOTWALL : 0) |
                      (hasalpha ? (DAMETH_HASALPHA|DAMETH_ONEBITALPHA) : 0));

        Xfree(pic);
    }

    polymost_setuptexture(dameth, -1);

    pth->picnum = dapic;
    pth->palnum = dapal;
    pth->shade = dashade;
    pth->effects = 0;
    pth->flags = TO_PTH_CLAMPED(dameth) | TO_PTH_NOTRANSFIX(dameth) | (hasalpha*(PTH_HASALPHA|PTH_ONEBITALPHA)) | (npoty*PTH_NPOTWALL);
    pth->hicr = NULL;
    pth->siz = tsiz;

#if defined USE_GLEXT && !defined EDUKE32_GLES
    if (!gotcache && glinfo.texcompr && glusetexcache && glusetexcompr == 2 && dapic < MAXUSERTILES)
    {
        cachead.quality = 0;
        cachead.xdim = tsiz.x;
        cachead.ydim = tsiz.y;

        cachead.flags = (check_nonpow2(siz.x) || check_nonpow2(siz.y)) * CACHEAD_NONPOW2 |
                        npoty * CACHEAD_NPOTWALL |
                        hasalpha * CACHEAD_HASALPHA | hasfullbright * CACHEAD_HASFULLBRIGHT | CACHEAD_NODOWNSIZE;

        texcache_writetex_fromdriver(texcacheid, &cachead);
    }
#endif

    if (hasfullbright && !fullbrightloadingpass)
    {
        // Load the ONLY texture that'll be assembled with the regular one to
        // make the final texture with fullbright pixels.
        fullbrightloadingpass = 1;

        if (!pth->ofb)
            pth->ofb = (pthtyp *)Xcalloc(1,sizeof(pthtyp));

        pth->flags |= PTH_HASFULLBRIGHT;

        gloadtile_art(dapic, dapal, -1, 0, (dameth & ~DAMETH_MASKPROPS) | DAMETH_MASK, pth->ofb, 1);

        fullbrightloadingpass = 0;
    }
}

int32_t gloadtile_hi(int32_t dapic,int32_t dapalnum, int32_t facen, hicreplctyp *hicr,
                            int32_t dameth, pthtyp *pth, int32_t doalloc, polytintflags_t effect)
{
    if (!hicr) return -1;

    char *fn;

    if (facen > 0)
    {
        if (!hicr->skybox || facen > 6 || !hicr->skybox->face[facen-1])
            return -1;

        fn = hicr->skybox->face[facen-1];
    }
    else
    {
        if (!hicr->filename)
            return -1;

        fn = hicr->filename;
    }

    buildvfs_kfd filh;
    if (EDUKE32_PREDICT_FALSE((filh = kopen4load(fn, 0)) == buildvfs_kfd_invalid))
    {
        OSD_Printf("hightile: %s (pic %d) not found\n", fn, dapic);
        return -2;
    }

    int32_t picfillen = kfilelength(filh);
    kclose(filh);	// FIXME: shouldn't have to do this. bug in cache1d.c

    int32_t startticks = timerGetTicks(), willprint = 0;

    char onebitalpha = 1;
    char hasalpha;
    texcacheheader cachead;
    char texcacheid[BMAX_PATH];
    texcache_calcid(texcacheid, fn, picfillen+(dapalnum<<8), DAMETH_NARROW_MASKPROPS(dameth), effect & HICTINT_IN_MEMORY);
    int32_t gotcache = texcache_readtexheader(texcacheid, &cachead, 0);
    vec2_t siz = { 0, 0 }, tsiz = { 0, 0 };

    if (gotcache && !texcache_loadtile(&cachead, &doalloc, pth))
    {
        tsiz = { cachead.xdim, cachead.ydim };
        hasalpha = !!(cachead.flags & CACHEAD_HASALPHA);
    }
    else
    {
        // CODEDUP: mdloadskin

        int32_t isart = 0;

        gotcache = 0;	// the compressed version will be saved to disk

        int32_t const length = kpzbufload(fn);
        if (length == 0)
            return -1;

        // tsizx/y = replacement texture's natural size
        // xsiz/y = 2^x size of replacement

#ifdef WITHKPLIB
        kpgetdim(kpzbuf,picfillen,&tsiz.x,&tsiz.y);
#endif

        if (tsiz.x == 0 || tsiz.y == 0)
        {
            if (artCheckUnitFileHeader((uint8_t *)kpzbuf, picfillen))
                return -1;

            tsiz = { B_LITTLE16(B_UNBUF16(&kpzbuf[16])), B_LITTLE16(B_UNBUF16(&kpzbuf[18])) };

            if (tsiz.x == 0 || tsiz.y == 0)
                return -1;

            isart = 1;
        }

        if (!glinfo.texnpot)
        {
            for (siz.x=1; siz.x<tsiz.x; siz.x+=siz.x) { }
            for (siz.y=1; siz.y<tsiz.y; siz.y+=siz.y) { }
        }
        else
            siz = tsiz;

        if (isart)
        {
            if (tsiz.x * tsiz.y + ARTv1_UNITOFFSET > picfillen)
                return -2;
        }

        int32_t const bytesperline = siz.x * sizeof(coltype);
        coltype *pic = (coltype *)Xcalloc(siz.y, bytesperline);

        static coltype *lastpic = NULL;
        static char *lastfn = NULL;
        static int32_t lastsize = 0;

        if (lastpic && lastfn && !Bstrcmp(lastfn,fn))
        {
            willprint=1;
            Bmemcpy(pic, lastpic, siz.x*siz.y*sizeof(coltype));
        }
        else
        {
            if (isart)
            {
                artConvertRGB((palette_t *)pic, (uint8_t *)&kpzbuf[ARTv1_UNITOFFSET], siz.x, tsiz.x, tsiz.y);
            }
#ifdef WITHKPLIB
            else
            {
                if (kprender(kpzbuf,picfillen,(intptr_t)pic,bytesperline,siz.x,siz.y))
                {
                    Xfree(pic);
                    return -2;
                }
            }
#endif

            willprint=2;

            if (hicprecaching)
            {
                lastfn = fn;  // careful...
                if (!lastpic)
                {
                    lastpic = (coltype *)Xmalloc(siz.x*siz.y*sizeof(coltype));
                    lastsize = siz.x*siz.y;
                }
                else if (lastsize < siz.x*siz.y)
                {
                    Xfree(lastpic);
                    lastpic = (coltype *)Xmalloc(siz.x*siz.y*sizeof(coltype));
                }
                if (lastpic)
                    Bmemcpy(lastpic, pic, siz.x*siz.y*sizeof(coltype));
            }
            else if (lastpic)
            {
                DO_FREE_AND_NULL(lastpic);
                lastfn = NULL;
                lastsize = 0;
            }
        }

        char *cptr = britable[gammabrightness ? 0 : curbrightness];

        polytint_t const & tint = hictinting[dapalnum];
        int32_t r = (glinfo.bgra) ? tint.r : tint.b;
        int32_t g = tint.g;
        int32_t b = (glinfo.bgra) ? tint.b : tint.r;

        char al = 255;

        for (bssize_t y = 0, j = 0; y < tsiz.y; ++y, j += siz.x)
        {
            coltype tcol, *rpptr = &pic[j];

            for (bssize_t x = 0; x < tsiz.x; ++x)
            {
                tcol.b = cptr[rpptr[x].b];
                tcol.g = cptr[rpptr[x].g];
                tcol.r = cptr[rpptr[x].r];
                al &= tcol.a = rpptr[x].a;
                onebitalpha &= tcol.a == 0 || tcol.a == 255;

                if (effect & HICTINT_GRAYSCALE)
                {
                    tcol.g = tcol.r = tcol.b = (uint8_t) ((tcol.b * GRAYSCALE_COEFF_RED) +
                                                          (tcol.g * GRAYSCALE_COEFF_GREEN) +
                                                          (tcol.r * GRAYSCALE_COEFF_BLUE));
                }

                if (effect & HICTINT_INVERT)
                {
                    tcol.b = 255 - tcol.b;
                    tcol.g = 255 - tcol.g;
                    tcol.r = 255 - tcol.r;
                }

                if (effect & HICTINT_COLORIZE)
                {
                    tcol.b = min((int32_t)((tcol.b) * r) >> 6, 255);
                    tcol.g = min((int32_t)((tcol.g) * g) >> 6, 255);
                    tcol.r = min((int32_t)((tcol.r) * b) >> 6, 255);
                }

                switch (effect & HICTINT_BLENDMASK)
                {
                    case HICTINT_BLEND_SCREEN:
                        tcol.b = 255 - (((255 - tcol.b) * (255 - r)) >> 8);
                        tcol.g = 255 - (((255 - tcol.g) * (255 - g)) >> 8);
                        tcol.r = 255 - (((255 - tcol.r) * (255 - b)) >> 8);
                        break;
                    case HICTINT_BLEND_OVERLAY:
                        tcol.b = tcol.b < 128 ? (tcol.b * r) >> 7 : 255 - (((255 - tcol.b) * (255 - r)) >> 7);
                        tcol.g = tcol.g < 128 ? (tcol.g * g) >> 7 : 255 - (((255 - tcol.g) * (255 - g)) >> 7);
                        tcol.r = tcol.r < 128 ? (tcol.r * b) >> 7 : 255 - (((255 - tcol.r) * (255 - b)) >> 7);
                        break;
                    case HICTINT_BLEND_HARDLIGHT:
                        tcol.b = r < 128 ? (tcol.b * r) >> 7 : 255 - (((255 - tcol.b) * (255 - r)) >> 7);
                        tcol.g = g < 128 ? (tcol.g * g) >> 7 : 255 - (((255 - tcol.g) * (255 - g)) >> 7);
                        tcol.r = b < 128 ? (tcol.r * b) >> 7 : 255 - (((255 - tcol.r) * (255 - b)) >> 7);
                        break;
                }

                rpptr[x] = tcol;
            }
        }

        hasalpha = (al != 255);
        onebitalpha &= hasalpha;

        if ((!(dameth & DAMETH_CLAMPED)) || facen) //Duplicate texture pixels (wrapping tricks for non power of 2 texture sizes)
        {
            if (siz.x > tsiz.x)  // Copy left to right
            {
                for (int32_t y = 0, *lptr = (int32_t *)pic; y < tsiz.y; y++, lptr += siz.x)
                    Bmemcpy(&lptr[tsiz.x], lptr, (siz.x - tsiz.x) << 2);
            }

            if (siz.y > tsiz.y)  // Copy top to bottom
                Bmemcpy(&pic[siz.x * tsiz.y], pic, (siz.y - tsiz.y) * siz.x << 2);
        }

        if (!glinfo.bgra)
        {
            for (bssize_t i=siz.x*siz.y, j=0; j<i; j++)
                swapchar(&pic[j].r, &pic[j].b);
        }

        // end CODEDUP

        if (tsiz.x>>r_downsize <= tilesiz[dapic].x || tsiz.y>>r_downsize <= tilesiz[dapic].y)
            hicr->flags |= HICR_ARTIMMUNITY;

        if ((doalloc&3)==1)
            glGenTextures(1, &pth->glpic); //# of textures (make OpenGL allocate structure)
        glBindTexture(GL_TEXTURE_2D, pth->glpic);

        fixtransparency(pic,tsiz,siz,dameth);

        int32_t const texfmt = glinfo.bgra ? GL_BGRA : GL_RGBA;

        if (!doalloc)
        {
            vec2_t pthSiz2 = pth->siz;
            if (!glinfo.texnpot)
            {
                for (pthSiz2.x=1; pthSiz2.x < pth->siz.x; pthSiz2.x+=pthSiz2.x) { }
                for (pthSiz2.y=1; pthSiz2.y < pth->siz.y; pthSiz2.y+=pthSiz2.y) { }
            }
            else
                pthSiz2 = tsiz;
            if (siz.x > pthSiz2.x ||
                siz.y > pthSiz2.y)
            {
                //POGO: grow our texture to hold the tile data
                doalloc = true;
            }
        }
        uploadtexture(doalloc,siz,texfmt,pic,tsiz,
                      dameth | DAMETH_HI | DAMETH_NOFIX |
                      TO_DAMETH_NODOWNSIZE(hicr->flags) |
                      TO_DAMETH_NOTEXCOMPRESS(hicr->flags) |
                      TO_DAMETH_ARTIMMUNITY(hicr->flags) |
                      (onebitalpha ? DAMETH_ONEBITALPHA : 0) |
                      (hasalpha ? DAMETH_HASALPHA : 0));

        Xfree(pic);
    }

    // precalculate scaling parameters for replacement
    if (facen > 0)
        pth->scale = { (float)tsiz.x * (1.0f/64.f), (float)tsiz.y * (1.0f/64.f) };
    else
        pth->scale = { (float)tsiz.x / (float)tilesiz[dapic].x, (float)tsiz.y / (float)tilesiz[dapic].y };

    polymost_setuptexture(dameth, (hicr->flags & HICR_FORCEFILTER) ? TEXFILTER_ON : -1);

    if (tsiz.x>>r_downsize <= tilesiz[dapic].x || tsiz.y>>r_downsize <= tilesiz[dapic].y)
        hicr->flags |= HICR_ARTIMMUNITY;

    pth->picnum = dapic;
    pth->effects = effect;
    pth->flags = TO_PTH_CLAMPED(dameth) | TO_PTH_NOTRANSFIX(dameth) |
                 PTH_HIGHTILE | ((facen>0) * PTH_SKYBOX) |
                 (onebitalpha ? PTH_ONEBITALPHA : 0) |
                 (hasalpha ? PTH_HASALPHA : 0) |
                 ((hicr->flags & HICR_FORCEFILTER) ? PTH_FORCEFILTER : 0);
    pth->skyface = facen;
    pth->hicr = hicr;
    pth->siz = tsiz;

#if defined USE_GLEXT && !defined EDUKE32_GLES
    if (!gotcache && glinfo.texcompr && glusetexcache && !(hicr->flags & HICR_NOTEXCOMPRESS) &&
        (glusetexcompr == 2 || (glusetexcompr && !(hicr->flags & HICR_ARTIMMUNITY))))
    {
        const int32_t nonpow2 = check_nonpow2(siz.x) || check_nonpow2(siz.y);

        // save off the compressed version
        cachead.quality = (hicr->flags & (HICR_NODOWNSIZE|HICR_ARTIMMUNITY)) ? 0 : r_downsize;
        cachead.xdim = tsiz.x >> cachead.quality;
        cachead.ydim = tsiz.y >> cachead.quality;

        // handle nodownsize:
        cachead.flags = nonpow2 * CACHEAD_NONPOW2 | (hasalpha ? CACHEAD_HASALPHA : 0) |
                        ((hicr->flags & (HICR_NODOWNSIZE|HICR_ARTIMMUNITY)) ? CACHEAD_NODOWNSIZE : 0);

        ///            OSD_Printf("Caching \"%s\"\n", fn);
        texcache_writetex_fromdriver(texcacheid, &cachead);

        if (willprint)
        {
            int32_t etime = timerGetTicks() - startticks;
            if (etime >= MIN_CACHETIME_PRINT)
                OSD_Printf("Load tile %4d: p%d-m%d-e%d %s... cached... %d ms\n", dapic, dapalnum, dameth, effect,
                           willprint == 2 ? fn : "", etime);
            willprint = 0;
        }
        else
            OSD_Printf("Cached \"%s\"\n", fn);
    }
#endif

    if (willprint)
    {
        int32_t etime = timerGetTicks()-startticks;
        if (etime>=MIN_CACHETIME_PRINT)
            OSD_Printf("Load tile %4d: p%d-m%d-e%d %s... %d ms\n", dapic, dapalnum, dameth, effect,
                       willprint==2 ? fn : "", etime);
    }

    return 0;
}

#ifdef USE_GLEXT
void polymost_setupdetailtexture(const int32_t texunits, const int32_t tex)
{
    glActiveTexture(texunits);

    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glClientActiveTexture(texunits);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

void polymost_setupglowtexture(const int32_t texunits, const int32_t tex)
{
    glActiveTexture(texunits);

    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glClientActiveTexture(texunits);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}
#endif


//(dpx,dpy) specifies an n-sided polygon. The polygon must be a convex clockwise loop.
//    n must be <= 8 (assume clipping can double number of vertices)
//method: 0:solid, 1:masked(255 is transparent), 2:transluscent #1, 3:transluscent #2
//    +4 means it's a sprite, so wraparound isn't needed

// drawpoly's hack globals
static int32_t pow2xsplit = 0, skyclamphack = 0, skyzbufferhack = 0, flatskyrender = 0;
static float drawpoly_alpha = 0.f;
static uint8_t drawpoly_blend = 0;

static inline pthtyp *our_texcache_fetch(int32_t dameth)
{
    return texcache_fetch(globalpicnum, globalpal, getpalookup(0, globalshade), dameth);
}

int32_t polymost_maskWallHasTranslucency(uwalltype const * const wall)
{
    if (wall->cstat & CSTAT_WALL_TRANSLUCENT)
        return true;

    //POGO: only hightiles may have translucency in their texture
    if (!usehightile)
        return false;

    uint8_t pal = wall->pal;
    if (palookup[pal] == NULL)
        pal = 0;

    pthtyp* pth = texcache_fetch(wall->picnum, pal, 0, DAMETH_MASK | DAMETH_WALL);
    return pth && (pth->flags & PTH_HASALPHA) && !(pth->flags & PTH_ONEBITALPHA);
}

int32_t polymost_spriteHasTranslucency(tspritetype const * const tspr)
{
    if ((tspr->cstat & CSTAT_SPRITE_TRANSLUCENT) || (tspr->clipdist & TSPR_FLAGS_DRAW_LAST) || 
        ((unsigned)tspr->owner < MAXSPRITES && spriteext[tspr->owner].alpha))
        return true;

    //POGO: only hightiles may have translucency in their texture
    if (!usehightile)
        return false;

    uint8_t pal = tspr->shade;
    if (palookup[pal] == NULL)
        pal = 0;

    pthtyp* pth = texcache_fetch(tspr->picnum, pal, 0, DAMETH_MASK | DAMETH_CLAMPED);
    return pth && (pth->flags & PTH_HASALPHA) && !(pth->flags & PTH_ONEBITALPHA);
}

int32_t polymost_spriteIsModelOrVoxel(tspritetype const * const tspr)
{
    if ((unsigned)tspr->owner < MAXSPRITES && spriteext[tspr->owner].flags&SPREXT_NOTMD)
        return false;

    if (usemodels && tile2model[Ptile2tile(tspr->picnum, tspr->pal)].modelid >= 0 &&
        tile2model[Ptile2tile(tspr->picnum, tspr->pal)].framenum >= 0)
        return true;

    if (usevoxels && (tspr->cstat & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_SLAB && tiletovox[tspr->picnum] >= 0 && voxmodels[tiletovox[tspr->picnum]])
        return true;

    if ((tspr->cstat & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_SLAB && voxmodels[tspr->picnum])
        return true;

    return false;
}

static void polymost2_drawVBO(GLenum mode,
                              int32_t vertexBufferID,
                              int32_t indexBufferID,
                              const int32_t numElements,
                              float projectionMatrix[4*4],
                              float modelViewMatrix[4*4],
                              int32_t dameth,
                              float texScale[2],
                              float texOffset[2],
                              char cullFaces)
{
    if (dameth == DAMETH_BACKFACECULL ||
    #ifdef YAX_ENABLE
        g_nodraw ||
    #endif
        (uint32_t)globalpicnum >= MAXTILES)
    {
        return;
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    if (cullFaces)
    {
        glEnable(GL_CULL_FACE);
    }
    //POGOTODO: this is temporary, the permanent fix is to not allow the transform to affect the windings in the first place in polymost2_drawSprite()
    if (cullFaces == 1)
    {
        glCullFace(GL_BACK);
    }
    else
    {
        glCullFace(GL_FRONT);
    }

    //POGOTODO: in the future, state changes like binding these buffers can be batched.  For now, just switch on every VBO rendered
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferID);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    if (palookup[globalpal] == NULL)
    {
        globalpal = 0;
    }

    //Load texture (globalpicnum)
    setgotpic(globalpicnum);
    if (!waloff[globalpicnum])
    {
        tileLoad(globalpicnum);
    }

    pthtyp *pth = our_texcache_fetch(dameth | (r_useindexedcolortextures ? DAMETH_INDEXED : 0));

    if (!pth)
    {
        if (editstatus)
        {
            Bsprintf(ptempbuf, "pth==NULL! (bad pal?) pic=%d pal=%d", globalpicnum, globalpal);
            polymost_printtext256(8,8, editorcolors[15],editorcolors[5], ptempbuf, 0);
        }
        return;
    }

    glActiveTexture(GL_TEXTURE1);
    //POGO: temporarily swapped out blankTextureID for 0 (as the blank texture has been moved into the dynamic tilesheets)
    glBindTexture(GL_TEXTURE_2D, (pth && pth->flags & PTH_HASFULLBRIGHT && r_fullbrights) ? pth->ofb->glpic : 0);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

    glActiveTexture(GL_TEXTURE0);
    polymost_bindPth(pth);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

    //POGOTODO: handle tinting & shading completely with fragment shader
    //POGOTODO: handle fullbright & glow completely with fragment shader

    //POGOTODO: glAlphaFunc is deprecated, move this into the fragment shader
    float const al = waloff[globalpicnum] ? alphahackarray[globalpicnum] != 0 ? alphahackarray[globalpicnum] * (1.f/255.f):
                             (pth && pth->hicr && pth->hicr->alphacut >= 0.f ? pth->hicr->alphacut : 0.f) : 0.f;
    glAlphaFunc(GL_GREATER, al);
    //POGOTODO: batch this, only apply it to sprites that actually need blending
    glEnable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);

    handle_blend((dameth & DAMETH_MASKPROPS) > DAMETH_MASK, drawpoly_blend, (dameth & DAMETH_MASKPROPS) == DAMETH_TRANS2);

    polymost_useShaderProgram(polymost2BasicShaderProgramID);

    //POGOTODO: batch uniform binding
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    polytint_t const & polytint = hictinting[globalpal];
    //POGOTODO: full bright pass uses its own globalshade...
    tint[0] = (1.f-(polytint.sr*(1.f/255.f)))*getshadefactor(globalshade)+(polytint.sr*(1.f/255.f));
    tint[1] = (1.f-(polytint.sg*(1.f/255.f)))*getshadefactor(globalshade)+(polytint.sg*(1.f/255.f));
    tint[2] = (1.f-(polytint.sb*(1.f/255.f)))*getshadefactor(globalshade)+(polytint.sb*(1.f/255.f));

    // spriteext full alpha control
    float alpha = float_trans(dameth & DAMETH_MASKPROPS, drawpoly_blend) * (1.f - drawpoly_alpha);

    if (pth)
    {
        // tinting
        polytintflags_t const tintflags = hictinting[globalpal].f;
        if (!(tintflags & HICTINT_PRECOMPUTED))
        {
            if (pth->flags & PTH_HIGHTILE)
            {
                if (pth->palnum != globalpal || (pth->effects & HICTINT_IN_MEMORY) || (tintflags & HICTINT_APPLYOVERALTPAL))
                    hictinting_apply(tint, globalpal);
            }
            else if (tintflags & (HICTINT_USEONART|HICTINT_ALWAYSUSEART))
                hictinting_apply(tint, globalpal);
        }

        // global tinting
        if ((pth->flags & PTH_HIGHTILE) && have_basepal_tint())
            hictinting_apply(tint, MAXPALOOKUPS-1);
    }

    glUniformMatrix4fv(projMatrixLoc, 1, false, projectionMatrix);
    glUniformMatrix4fv(mvMatrixLoc, 1, false, modelViewMatrix);
    glUniform1i(texSamplerLoc, 0);
    glUniform1i(fullBrightSamplerLoc, 1);
    glUniform2fv(texOffsetLoc, 1, texOffset);
    glUniform2fv(texScaleLoc, 1, texScale);
    glUniform4fv(tintLoc, 1, tint);
    glUniform1f(alphaLoc, alpha);
    const float fogRange[2] = {fogresult, fogresult2};
    glUniform2fv(fogRangeLoc, 1, fogRange);
    glUniform4fv(fogColorLoc, 1, (GLfloat*) &fogcol);

    if (indexBufferID == 0)
    {
        glDrawArrays(mode,
                     0,
                     numElements);
    }
    else
    {
        glDrawElements(mode,
                       numElements,
                       GL_UNSIGNED_SHORT,
                       0);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    //POGOTODO: again, these state changes should be batched in the future, rather than on each VBO rendered
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glDisable(GL_CULL_FACE);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    //polymost_resetVertexPointers();
}

void polymost_updatePalette()
{
    if (videoGetRenderMode() != REND_POLYMOST)
        return;

    polymost_setPalswap(globalpal);
    polymost_setShade(globalshade);

    //POGO: only bind the base pal once when it's swapped
    if (curbasepal != lastbasepal)
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, paletteTextureIDs[curbasepal]);
        lastbasepal = curbasepal;
        glActiveTexture(GL_TEXTURE0);
    }
}

static void polymost_lockSubBuffer(uint32_t subBufferIndex)
{
    if (drawpolyVertsSync[subBufferIndex])
    {
        glDeleteSync(drawpolyVertsSync[subBufferIndex]);
    }

    drawpolyVertsSync[subBufferIndex] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

static void polymost_waitForSubBuffer(uint32_t subBufferIndex)
{
    if (drawpolyVertsSync[subBufferIndex])
    {
        while (true)
        {
            // we only need to flush if there's a possibility that drawpolyVertsBufferLength is
            // so small that we can eat through 3 times the buffer size in a single frame
            GLenum waitResult = glClientWaitSync(drawpolyVertsSync[subBufferIndex], GL_SYNC_FLUSH_COMMANDS_BIT, 500000);
            if (waitResult == GL_ALREADY_SIGNALED ||
                waitResult == GL_CONDITION_SATISFIED)
            {
                return;
            }
            if (waitResult == GL_WAIT_FAILED)
            {
                OSD_Printf("polymost_waitForSubBuffer: Wait failed! Error 0x%X. Disabling r_persistentStreamBuffer.\n", glGetError());
                r_persistentStreamBuffer = 0;
                videoResetMode();
                if (videoSetGameMode(fullscreen,xres,yres,bpp,upscalefactor))
                {
                    OSD_Printf("polymost_waitForSubBuffer: Video reset failed.  Please ensure r_persistentStreamBuffer = 0 and try restarting the game.\n");
                    Bexit(EXIT_FAILURE);
                }
                return;
            }

            static char loggedLongWait = false;
            if (waitResult == GL_TIMEOUT_EXPIRED &&
                !loggedLongWait)
            {
                OSD_Printf("polymost_waitForSubBuffer(): Had to wait for the drawpoly buffer to become available.  For performance, try increasing buffer size with r_drawpolyVertsBufferLength.\n");
                loggedLongWait = true;
            }
        }
    }
}

static void polymost_updaterotmat(void)
{
    if (currentShaderProgramID == polymost1CurrentShaderProgramID)
    {
        float matrix[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f,
        };
#if !SOFTROTMAT
        //Up/down rotation
        float udmatrix[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, gchang, -gshang*gvrcorrection, 0.f,
            0.f, gshang/gvrcorrection, gchang, 0.f,
            0.f, 0.f, 0.f, 1.f,
        };
        // Tilt rotation
        float tiltmatrix[16] = {
            gctang, -gstang, 0.f, 0.f,
            gstang, gctang, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f,
        };
        multiplyMatrix4f(matrix, udmatrix);
        multiplyMatrix4f(matrix, tiltmatrix);
#endif
        Bmemcpy(polymost1RotMatrix, matrix, sizeof(matrix));
        glUniformMatrix4fv(polymost1RotMatrixLoc, 1, false, polymost1RotMatrix);
    }
}

static void polymost_identityrotmat(void)
{
    if (currentShaderProgramID == polymost1CurrentShaderProgramID)
    {
        float matrix[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f,
        };
        Bmemcpy(polymost1RotMatrix, matrix, sizeof(matrix));
        glUniformMatrix4fv(polymost1RotMatrixLoc, 1, false, polymost1RotMatrix);
    }
}

static void polymost_polyeditorfunc(vec2f_t const * const dpxy, int n)
{
    if (!doeditorcheck)
        return;

    for (int i = 0; i < n; i++)
    {
        float dx1 = dpxy[(i+1)%n].x-dpxy[i].x;
        float dy1 = dpxy[(i+1)%n].y-dpxy[i].y;
        float dx2 = fsearchx-dpxy[i].x;
        float dy2 = fsearchy-dpxy[i].y;
        float cross = dx1*dy2-dx2*dy1;
        if (cross < 0.f)
            return;
    }

    float const z = otex.d + xtex.d * fsearchx + ytex.d * fsearchy;

    if (z > fsearchz)
    {
        searchit = 1;
        searchsector = psectnum;
        searchwall = pwallnum;
        searchbottomwall = pbottomwall;
        searchisbottom = pisbottomwall;
        searchstat = psearchstat;
        fsearchz = z;
    }
}

static void polymost_flatskyrender(vec2f_t const* const dpxy, int32_t const n, int32_t method);

static void polymost_drawpoly(vec2f_t const * const dpxy, int32_t const n, int32_t method)
{
    if (doeditorcheck && editstatus)
        polymost_polyeditorfunc(dpxy, n);

    if (method == DAMETH_BACKFACECULL ||
#ifdef YAX_ENABLE
        g_nodraw ||
#endif
        (uint32_t)globalpicnum >= MAXTILES)
        return;

    const int32_t method_ = method;

    if (n == 3)
    {
        if ((dpxy[0].x-dpxy[1].x) * (dpxy[2].y-dpxy[1].y) >=
            (dpxy[2].x-dpxy[1].x) * (dpxy[0].y-dpxy[1].y)) return; //for triangle
    }
    else if (n > 3)
    {
        float f = 0; //f is area of polygon / 2

        for (bssize_t i=n-2, j=n-1,k=0; k<n; i=j,j=k,k++)
            f += (dpxy[i].x-dpxy[k].x)*dpxy[j].y;

        if (f <= 0) return;
    }

    static int32_t skyzbufferhack_pass = 0;
    if (flatskyrender && skyzbufferhack_pass == 0)
    {
        polymost_flatskyrender(dpxy, n, method);
        return;
    }

    if (palookup[globalpal] == NULL)
        globalpal = 0;

    //Load texture (globalpicnum)
    setgotpic(globalpicnum);
    vec2_16_t const & tsizart = tilesiz[globalpicnum];
    vec2_t tsiz = { tsizart.x, tsizart.y };

    if (!waloff[globalpicnum])
    {
        tileLoad(globalpicnum);
    }

    Bassert(n <= MAX_DRAWPOLY_VERTS);

    int j = 0;
    float px[8], py[8], dd[8], uu[8], vv[8];
#if SOFTROTMAT
    float const ozgs = (ghalfx / gvrcorrection) * gshang,
                ozgc = (ghalfx / gvrcorrection) * gchang;
#endif

    for (bssize_t i=0; i<n; ++i)
    {
#if SOFTROTMAT
        //Up/down rotation
        vec3f_t const orot = { dpxy[i].x - ghalfx,
                              (dpxy[i].y - ghoriz) * gchang - ozgs,
                              (dpxy[i].y - ghoriz) * gshang + ozgc };

        // Tilt rotation
        float const r = (ghalfx / gvrcorrection) / orot.z;

        px[j] = ghalfx + (((orot.x * gctang) - (orot.y * gstang)) * r);
        py[j] = ghoriz + (((orot.x * gstang) + (orot.y * gctang)) * r);

        dd[j] = (dpxy[i].x * xtex.d + dpxy[i].y * ytex.d + otex.d) * r;
        if (dd[j] <= 0.f) // invalid polygon
            return;
        uu[j] = (dpxy[i].x * xtex.u + dpxy[i].y * ytex.u + otex.u) * r;
        vv[j] = (dpxy[i].x * xtex.v + dpxy[i].y * ytex.v + otex.v) * r;

        if ((!j) || (px[j] != px[j-1]) || (py[j] != py[j-1]))
            j++;
#else
        px[j] = dpxy[i].x;
        py[j] = dpxy[i].y;

        dd[j] = (dpxy[i].x * xtex.d + dpxy[i].y * ytex.d + otex.d);
        if (dd[j] <= 0.f) // invalid polygon
            return;
        uu[j] = (dpxy[i].x * xtex.u + dpxy[i].y * ytex.u + otex.u);
        vv[j] = (dpxy[i].x * xtex.v + dpxy[i].y * ytex.v + otex.v);
        j++;
#endif
    }

    while ((j >= 3) && (px[j-1] == px[0]) && (py[j-1] == py[0])) j--;

    if (j < 3)
        return;

    int const npoints = j;

    if (skyclamphack) method |= DAMETH_CLAMPED;

    polymost_outputGLDebugMessage(3, "polymost_drawpoly(dpxy:%p, n:%d, method_:%X), method: %X", dpxy, n, method_, method);

    pthtyp *pth = our_texcache_fetch(method | (videoGetRenderMode() == REND_POLYMOST && r_useindexedcolortextures ? DAMETH_INDEXED : 0));

    if (!pth)
    {
        if (editstatus)
        {
            Bsprintf(ptempbuf, "pth==NULL! (bad pal?) pic=%d pal=%d", globalpicnum, globalpal);
            polymost_printtext256(8,8, editorcolors[15],editorcolors[5], ptempbuf, 0);
        }
        return;
    }

    if (!waloff[globalpicnum])
    {
        tsiz.x = tsiz.y = 1;
        glColorMask(false, false, false, false); //Hack to update Z-buffer for invalid mirror textures
    }

    static int32_t fullbright_pass = 0;

    if (pth->flags & PTH_HASFULLBRIGHT && r_fullbrights)
    {
        if (!fullbright_pass)
            fullbright_pass = 1;
        else if (fullbright_pass == 2)
            pth = pth->ofb;
    }

    Bassert(pth);

    // If we aren't rendmode 3, we're in Polymer, which means this code is
    // used for rotatesprite only. Polymer handles all the material stuff,
    // just submit the geometry and don't mess with textures.
    if (videoGetRenderMode() == REND_POLYMOST)
    {
        polymost_bindPth(pth);

        //POGOTODO: I could move this into bindPth
        if (!(pth->flags & PTH_INDEXED))
            polymost_usePaletteIndexing(false);
        else if (polymost_usetileshades())
            polymost_setFogEnabled(false);

        if (drawpoly_srepeat)
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        if (drawpoly_trepeat)
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    }

    // texture scale by parkar request
    if (pth->hicr && !drawingskybox && ((pth->hicr->scale.x != 1.0f) || (pth->hicr->scale.y != 1.0f)))
    {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glScalef(pth->hicr->scale.x, pth->hicr->scale.y, 1.0f);
        glMatrixMode(GL_MODELVIEW);
    }

#ifdef USE_GLEXT
    int32_t texunits = GL_TEXTURE0;

    if (videoGetRenderMode() == REND_POLYMOST)
    {
        polymost_updatePalette();
        texunits += 4;
    }

    // detail texture
    if (r_detailmapping)
    {
        pthtyp *detailpth = NULL;

        if (usehightile && !drawingskybox && hicfindsubst(globalpicnum, DETAILPAL, 1) &&
            (detailpth = texcache_fetch(globalpicnum, DETAILPAL, 0, method & ~DAMETH_MASKPROPS)) &&
            detailpth->hicr && detailpth->hicr->palnum == DETAILPAL)
        {
            polymost_useDetailMapping(true);
            polymost_setupdetailtexture(videoGetRenderMode() == REND_POLYMOST ? GL_TEXTURE3 : ++texunits, detailpth->glpic);

            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();

            if (pth->hicr && ((pth->hicr->scale.x != 1.0f) || (pth->hicr->scale.y != 1.0f)))
                glScalef(pth->hicr->scale.x, pth->hicr->scale.y, 1.0f);

            if ((detailpth->hicr->scale.x != 1.0f) || (detailpth->hicr->scale.y != 1.0f))
                glScalef(detailpth->hicr->scale.x, detailpth->hicr->scale.y, 1.0f);

            glMatrixMode(GL_MODELVIEW);
            glActiveTexture(GL_TEXTURE0);
        }
    }

    // glow texture
    if (r_glowmapping)
    {
        pthtyp *glowpth = NULL;

        if (usehightile && !drawingskybox && hicfindsubst(globalpicnum, GLOWPAL, 1) &&
            (glowpth = texcache_fetch(globalpicnum, GLOWPAL, 0, (method & ~DAMETH_MASKPROPS) | DAMETH_MASK)) &&
            glowpth->hicr && (glowpth->hicr->palnum == GLOWPAL))
        {
            polymost_useGlowMapping(true);
            polymost_setupglowtexture(videoGetRenderMode() == REND_POLYMOST ? GL_TEXTURE4 : ++texunits, glowpth->glpic);
            glActiveTexture(GL_TEXTURE0);
        }
    }

    if (glinfo.texnpot && r_npotwallmode == 2 && (method & DAMETH_WALL) != 0)
    {
        int32_t size = tilesiz[globalpicnum].y;
        int32_t size2;
        for (size2 = 1; size2 < size; size2 += size2) {}
        if (size == size2)
            polymost_npotEmulation(false, 1.f, 0.f);
        else
        {
            float xOffset = 1.f / tilesiz[globalpicnum].x;
            polymost_npotEmulation(true, (1.f*size2) / size, xOffset);
        }
    }
    else
    {
        polymost_npotEmulation(false, 1.f, 0.f);
    }
#endif

    vec2f_t hacksc = { 1.f, 1.f };

    if (pth->flags & PTH_HIGHTILE)
    {
        hacksc = pth->scale;
        tsiz = pth->siz;
    }

    vec2_t tsiz2 = tsiz;

    if (!glinfo.texnpot)
    {
        for (tsiz2.x = 1; tsiz2.x < tsiz.x; tsiz2.x += tsiz2.x)
            ; /* do nothing */
        for (tsiz2.y = 1; tsiz2.y < tsiz.y; tsiz2.y += tsiz2.y)
            ; /* do nothing */
    }

    if (method & DAMETH_MASKPROPS || fullbright_pass == 2)
    {
        float const al = alphahackarray[globalpicnum] != 0 ? alphahackarray[globalpicnum] * (1.f/255.f) :
                         (pth->hicr && pth->hicr->alphacut >= 0.f ? pth->hicr->alphacut : 0.f);

        glAlphaFunc(GL_GREATER, al);
        handle_blend((method & DAMETH_MASKPROPS) > DAMETH_MASK, drawpoly_blend, (method & DAMETH_MASKPROPS) == DAMETH_TRANS2);
    }

    float pc[4];

#ifdef POLYMER
    if (videoGetRenderMode() == REND_POLYMER && pr_artmapping && !(globalflags & GLOBAL_NO_GL_TILESHADES) && polymer_eligible_for_artmap(globalpicnum, pth))
        pc[0] = pc[1] = pc[2] = 1.0f;
    else
#endif
    {
        polytint_t const & tint = hictinting[globalpal];
        float shadeFactor = (pth->flags & PTH_INDEXED) && polymost_usetileshades() ? 1.f : getshadefactor(globalshade);
        pc[0] = (1.f-(tint.sr*(1.f/255.f)))*shadeFactor+(tint.sr*(1.f/255.f));
        pc[1] = (1.f-(tint.sg*(1.f/255.f)))*shadeFactor+(tint.sg*(1.f/255.f));
        pc[2] = (1.f-(tint.sb*(1.f/255.f)))*shadeFactor+(tint.sb*(1.f/255.f));
    }

    // spriteext full alpha control
    pc[3] = float_trans(method & DAMETH_MASKPROPS, drawpoly_blend) * (1.f - drawpoly_alpha);

    // tinting
    polytintflags_t const tintflags = hictinting[globalpal].f;
    if (!(tintflags & HICTINT_PRECOMPUTED))
    {
        if (pth->flags & PTH_HIGHTILE)
        {
            if (pth->palnum != globalpal || (pth->effects & HICTINT_IN_MEMORY) || (tintflags & HICTINT_APPLYOVERALTPAL))
                hictinting_apply(pc, globalpal);
        }
        else if (tintflags & (HICTINT_USEONART|HICTINT_ALWAYSUSEART))
            hictinting_apply(pc, globalpal);
    }

    // global tinting
    if ((pth->flags & PTH_HIGHTILE) && have_basepal_tint())
        hictinting_apply(pc, MAXPALOOKUPS-1);

    globaltinting_apply(pc);

    if (skyzbufferhack_pass)
        pc[3] = 0.01f;

    glColor4f(pc[0], pc[1], pc[2], pc[3]);

    //POGOTODO: remove this, replace it with a shader implementation
    //Hack for walls&masked walls which use textures that are not a power of 2
    if ((pow2xsplit) && (tsiz.x != tsiz2.x))
    {
        vec3f_t const opxy[3] = { { py[1] - py[2], py[2] - py[0], py[0] - py[1] },
                                  { px[2] - px[1], px[0] - px[2], px[1] - px[0] },
                                  { px[0] - .5f, py[0] - .5f, 0 } };

        float const r = 1.f / (opxy[0].x*px[0] + opxy[0].y*px[1] + opxy[0].z*px[2]);

        vec3f_t ngx = { (opxy[0].x * dd[0] + opxy[0].y * dd[1] + opxy[0].z * dd[2]) * r,
                        ((opxy[0].x * uu[0] + opxy[0].y * uu[1] + opxy[0].z * uu[2]) * r) * hacksc.x,
                        ((opxy[0].x * vv[0] + opxy[0].y * vv[1] + opxy[0].z * vv[2]) * r) * hacksc.y };

        vec3f_t ngy = { (opxy[1].x * dd[0] + opxy[1].y * dd[1] + opxy[1].z * dd[2]) * r,
                        ((opxy[1].x * uu[0] + opxy[1].y * uu[1] + opxy[1].z * uu[2]) * r) * hacksc.x,
                        ((opxy[1].x * vv[0] + opxy[1].y * vv[1] + opxy[1].z * vv[2]) * r) * hacksc.y };

        vec3f_t ngo = { dd[0] - opxy[2].x * ngx.d - opxy[2].y * ngy.d,
                        (uu[0] - opxy[2].x * ngx.u - opxy[2].y * ngy.u) * hacksc.x,
                        (vv[0] - opxy[2].x * ngx.v - opxy[2].y * ngy.v) * hacksc.y };

        float const uoffs = ((float)(tsiz2.x - tsiz.x) * 0.5f);

        ngx.u -= ngx.d * uoffs;
        ngy.u -= ngy.d * uoffs;
        ngo.u -= ngo.d * uoffs;

        float du0 = 0.f, du1 = 0.f;

        //Find min&max u coordinates (du0...du1)
        for (bssize_t i=0; i<npoints; ++i)
        {
            vec2f_t const o = { px[i], py[i] };
            float const f = (o.x*ngx.u + o.y*ngy.u + ngo.u) / (o.x*ngx.d + o.y*ngy.d + ngo.d);
            if (!i) { du0 = du1 = f; continue; }
            if (f < du0) du0 = f;
            else if (f > du1) du1 = f;
        }

        float const rf = 1.0f / tsiz.x;
        int const ix1 = (int)floorf(du1 * rf);

        for (bssize_t ix0 = (int)floorf(du0 * rf); ix0 <= ix1; ++ix0)
        {
            du0 = (float)(ix0 * tsiz.x);        // + uoffs;
            du1 = (float)((ix0 + 1) * tsiz.x);  // + uoffs;

            float duj = (px[0]*ngx.u + py[0]*ngy.u + ngo.u) / (px[0]*ngx.d + py[0]*ngy.d + ngo.d);
            int i = 0, nn = 0;

            do
            {
                j = i + 1;

                if (j == npoints)
                    j = 0;

                float const dui = duj;

                duj = (px[j]*ngx.u + py[j]*ngy.u + ngo.u) / (px[j]*ngx.d + py[j]*ngy.d + ngo.d);

                if ((du0 <= dui) && (dui <= du1))
                {
                    uu[nn] = px[i];
                    vv[nn] = py[i];
                    nn++;
                }

                //ox*(ngx.u-ngx.d*du1) + oy*(ngy.u-ngdy*du1) + (ngo.u-ngo.d*du1) = 0
                //(px[j]-px[i])*f + px[i] = ox
                //(py[j]-py[i])*f + py[i] = oy

                ///Solve for f
                //((px[j]-px[i])*f + px[i])*(ngx.u-ngx.d*du1) +
                //((py[j]-py[i])*f + py[i])*(ngy.u-ngdy*du1) + (ngo.u-ngo.d*du1) = 0

                auto mathyMcMatherson = [&](float const f) {
                    float const ff = -(px[i] * (ngx.u - ngx.d * f) + py[i] * (ngy.u - ngy.d * f) + (ngo.u - ngo.d * f))
                                    / ((px[j] - px[i]) * (ngx.u - ngx.d * f) + (py[j] - py[i]) * (ngy.u - ngy.d * f));
                    uu[nn] = (px[j] - px[i]) * ff + px[i];
                    vv[nn] = (py[j] - py[i]) * ff + py[i];
                    ++nn;
                };

                if (duj <= dui)
                {
                    if ((du1 < duj) != (du1 < dui)) mathyMcMatherson(du1);
                    if ((du0 < duj) != (du0 < dui)) mathyMcMatherson(du0);
                }
                else
                {
                    if ((du0 < duj) != (du0 < dui)) mathyMcMatherson(du0);
                    if ((du1 < duj) != (du1 < dui)) mathyMcMatherson(du1);
                }

                i = j;
            }
            while (i);

            if (nn < 3) continue;

            if (nn+drawpolyVertsOffset > (drawpolyVertsSubBufferIndex+1)*drawpolyVertsBufferLength)
            {
                if (persistentStreamBuffer)
                {
                    // lock this sub buffer
                    polymost_lockSubBuffer(drawpolyVertsSubBufferIndex);
                    drawpolyVertsSubBufferIndex = (drawpolyVertsSubBufferIndex+1)%3;
                    drawpolyVertsOffset = drawpolyVertsSubBufferIndex*drawpolyVertsBufferLength;
                    // wait for the next sub buffer to become available before writing to it
                    // our buffer size should be long enough that no waiting is ever necessary
                    polymost_waitForSubBuffer(drawpolyVertsSubBufferIndex);
                }
                else
                {
                    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*5*drawpolyVertsBufferLength, NULL, GL_STREAM_DRAW);
                    drawpolyVertsOffset = 0;
                }
            }

            vec2f_t const invtsiz2 = { 1.f / tsiz2.x, 1.f / tsiz2.y };
            uint32_t off = persistentStreamBuffer ? drawpolyVertsOffset : 0;
            for (i = 0; i<nn; ++i)
            {
                vec2f_t const o = { uu[i], vv[i] };
                vec3f_t const p = { o.x * ngx.d + o.y * ngy.d + ngo.d,
                                    o.x * ngx.u + o.y * ngy.u + ngo.u,
                                    o.x * ngx.v + o.y * ngy.v + ngo.v };
                float const r = 1.f/p.d;

                //update verts
                drawpolyVerts[(off+i)*5] = (o.x - ghalfx) * r * grhalfxdown10x;
                drawpolyVerts[(off+i)*5+1] = (ghalfy - o.y) * r * grhalfxdown10;
                drawpolyVerts[(off+i)*5+2] = r * (1.f / 1024.f);

                //update texcoords
                drawpolyVerts[(off+i)*5+3] = (p.u * r - du0 + uoffs) * invtsiz2.x;
                drawpolyVerts[(off+i)*5+4] = p.v * r * invtsiz2.y;
            }

            if (!persistentStreamBuffer)
            {
                glBufferSubData(GL_ARRAY_BUFFER, drawpolyVertsOffset*sizeof(float)*5, nn*sizeof(float)*5, drawpolyVerts);
            }
            glDrawArrays(GL_TRIANGLE_FAN, drawpolyVertsOffset, nn);
            drawpolyVertsOffset += nn;
        }
    }
    else
    {
        if (npoints+drawpolyVertsOffset > (drawpolyVertsSubBufferIndex+1)*drawpolyVertsBufferLength)
        {
            if (persistentStreamBuffer)
            {
                // lock this sub buffer
                polymost_lockSubBuffer(drawpolyVertsSubBufferIndex);
                drawpolyVertsSubBufferIndex = (drawpolyVertsSubBufferIndex+1)%3;
                drawpolyVertsOffset = drawpolyVertsSubBufferIndex*drawpolyVertsBufferLength;
                // wait for the next sub buffer to become available before writing to it
                // our buffer size should be long enough that no waiting is ever necessary
                polymost_waitForSubBuffer(drawpolyVertsSubBufferIndex);
            }
            else
            {
                glBufferData(GL_ARRAY_BUFFER, sizeof(float)*5*drawpolyVertsBufferLength, NULL, GL_STREAM_DRAW);
                drawpolyVertsOffset = 0;
            }
        }

        vec2f_t const scale = { 1.f / tsiz2.x * hacksc.x, 1.f / tsiz2.y * hacksc.y };
        uint32_t off = persistentStreamBuffer ? drawpolyVertsOffset : 0;
        for (bssize_t i = 0; i < npoints; ++i)
        {
            float const r = 1.f / dd[i];

            //update verts
            drawpolyVerts[(off+i)*5] = (px[i] - ghalfx) * r * grhalfxdown10x;
            drawpolyVerts[(off+i)*5+1] = (ghalfy - py[i]) * r * grhalfxdown10;
            drawpolyVerts[(off+i)*5+2] = r * (1.f / 1024.f);

            //update texcoords
            drawpolyVerts[(off+i)*5+3] = uu[i] * r * scale.x;
            drawpolyVerts[(off+i)*5+4] = vv[i] * r * scale.y;
        }

        if (!persistentStreamBuffer)
        {
            glBufferSubData(GL_ARRAY_BUFFER, drawpolyVertsOffset*sizeof(float)*5, npoints*sizeof(float)*5, drawpolyVerts);
        }
        glDrawArrays(GL_TRIANGLE_FAN, drawpolyVertsOffset, npoints);
        drawpolyVertsOffset += npoints;
    }

#ifdef USE_GLEXT
    if (videoGetRenderMode() != REND_POLYMOST)
    {
        while (texunits > GL_TEXTURE0)
        {
            glActiveTexture(texunits);
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);

            glClientActiveTexture(texunits);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);

            glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);

            --texunits;
        }
    }

    polymost_useDetailMapping(false);
    polymost_useGlowMapping(false);
    polymost_npotEmulation(false, 1.f, 0.f);
#endif
    if (pth->hicr)
    {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }

    if (videoGetRenderMode() != REND_POLYMOST)
    {
        if (!waloff[globalpicnum])
            glColorMask(true, true, true, true);

        return;
    }

    if (!(pth->flags & PTH_INDEXED))
    {
        // restore palette usage if we were just rendering a non-indexed color texture
        polymost_usePaletteIndexing(true);
    }
    else if (!nofog)
        polymost_setFogEnabled(true);

    int const clamp_mode = glinfo.clamptoedge ? GL_CLAMP_TO_EDGE : GL_CLAMP;

    if (drawpoly_srepeat)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_mode);

    if (drawpoly_trepeat)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp_mode);

    if (fullbright_pass == 1)
    {
        int32_t const shade = globalshade;

        globalshade = -128;
        fullbright_pass = 2;

        polymost_setFogEnabled(false);

        glDepthFunc(GL_EQUAL);

        polymost_drawpoly(dpxy, n, method_);

        glDepthFunc(GL_LEQUAL);

        if (!nofog)
            polymost_setFogEnabled(true);

        globalshade = shade;
        fullbright_pass = 0;
    }

    if (skyzbufferhack && skyzbufferhack_pass == 0)
    {
        vec3d_t const bxtex = xtex, bytex = ytex, botex = otex;
        xtex = xtex2, ytex = ytex2, otex = otex2;
        skyzbufferhack_pass++;
        glColorMask(false, false, false, false);
        polymost_drawpoly(dpxy, n, DAMETH_MASK);
        glColorMask(true, true, true, true);
        xtex = bxtex, ytex = bytex, otex = botex;
        skyzbufferhack_pass--;
    }

    if (!waloff[globalpicnum])
        glColorMask(true, true, true, true);
}


static inline void vsp_finalize_init(int32_t const vcnt)
{
    for (bssize_t i=0; i<vcnt; ++i)
    {
        vsp[i].cy[1] = vsp[i+1].cy[0]; vsp[i].ctag = i;
        vsp[i].fy[1] = vsp[i+1].fy[0]; vsp[i].ftag = i;
        vsp[i].n = i+1; vsp[i].p = i-1;
//        vsp[i].tag = -1;
    }
    vsp[vcnt-1].n = 0; vsp[0].p = vcnt-1;

    //VSPMAX-1 is dummy empty node
    for (bssize_t i=vcnt; i<VSPMAX; i++) { vsp[i].n = i+1; vsp[i].p = i-1; }
    vsp[VSPMAX-1].n = vcnt; vsp[vcnt].p = VSPMAX-1;
}

#ifdef YAX_ENABLE
static inline void yax_vsp_finalize_init(int32_t const yaxbunch, int32_t const vcnt)
{
    for (bssize_t i=0; i<vcnt; ++i)
    {
        yax_vsp[yaxbunch][i].cy[1] = yax_vsp[yaxbunch][i+1].cy[0]; yax_vsp[yaxbunch][i].ctag = i;
        yax_vsp[yaxbunch][i].n = i+1; yax_vsp[yaxbunch][i].p = i-1;
//        vsp[i].tag = -1;
    }
    yax_vsp[yaxbunch][vcnt-1].n = 0; yax_vsp[yaxbunch][0].p = vcnt-1;

    //VSPMAX-1 is dummy empty node
    for (bssize_t i=vcnt; i<VSPMAX; i++) { yax_vsp[yaxbunch][i].n = i+1; yax_vsp[yaxbunch][i].p = i-1; }
    yax_vsp[yaxbunch][VSPMAX-1].n = vcnt; yax_vsp[yaxbunch][vcnt].p = VSPMAX-1;
}
#endif

#define COMBINE_STRIPS

#ifdef COMBINE_STRIPS
static inline void vsdel(int const i)
{
    //Delete i
    int const pi = vsp[i].p;
    int const ni = vsp[i].n;

    vsp[ni].p = pi;
    vsp[pi].n = ni;

    //Add i to empty list
    vsp[i].n = vsp[VSPMAX-1].n;
    vsp[i].p = VSPMAX-1;
    vsp[vsp[VSPMAX-1].n].p = i;
    vsp[VSPMAX-1].n = i;
}

static inline void vsmerge(int const i, int const ni)
{
    vsp[i].cy[1] = vsp[ni].cy[1];
    vsp[i].fy[1] = vsp[ni].fy[1];
    vsdel(ni);
}

# ifdef YAX_ENABLE
static inline void yax_vsdel(int const yaxbunch, int const i)
{
    //Delete i
    int const pi = yax_vsp[yaxbunch][i].p;
    int const ni = yax_vsp[yaxbunch][i].n;

    yax_vsp[yaxbunch][ni].p = pi;
    yax_vsp[yaxbunch][pi].n = ni;

    //Add i to empty list
    yax_vsp[yaxbunch][i].n = yax_vsp[yaxbunch][VSPMAX - 1].n;
    yax_vsp[yaxbunch][i].p = VSPMAX - 1;
    yax_vsp[yaxbunch][yax_vsp[yaxbunch][VSPMAX - 1].n].p = i;
    yax_vsp[yaxbunch][VSPMAX - 1].n = i;
}
# endif
#endif

static inline int32_t vsinsaft(int const i)
{
    //i = next element from empty list
    int32_t const r = vsp[VSPMAX-1].n;
    vsp[vsp[r].n].p = VSPMAX-1;
    vsp[VSPMAX-1].n = vsp[r].n;

    vsp[r] = vsp[i]; //copy i to r

    //insert r after i
    vsp[r].p = i; vsp[r].n = vsp[i].n;
    vsp[vsp[i].n].p = r; vsp[i].n = r;

    return r;
}

#ifdef YAX_ENABLE
static inline int32_t yax_vsinsaft(int const yaxbunch, int const i)
{
    //i = next element from empty list
    int32_t const r = yax_vsp[yaxbunch][VSPMAX - 1].n;
    yax_vsp[yaxbunch][yax_vsp[yaxbunch][r].n].p = VSPMAX - 1;
    yax_vsp[yaxbunch][VSPMAX - 1].n = yax_vsp[yaxbunch][r].n;

    yax_vsp[yaxbunch][r] = yax_vsp[yaxbunch][i]; //copy i to r

    //insert r after i
    yax_vsp[yaxbunch][r].p = i; yax_vsp[yaxbunch][r].n = yax_vsp[yaxbunch][i].n;
    yax_vsp[yaxbunch][yax_vsp[yaxbunch][i].n].p = r; yax_vsp[yaxbunch][i].n = r;

    return r;
}
#endif

static int32_t domostpolymethod = DAMETH_NOMASK;

#define DOMOST_OFFSET .01f

static void polymost_clipmost(vec2f_t *dpxy, int &n, float x0, float x1, float y0top, float y0bot, float y1top, float y1bot)
{
    if (y0bot < y0top || y1bot < y1top)
        return;

    //Clip to (x0,y0top)-(x1,y1top)

    vec2f_t dp2[8];

    float t0, t1;
    int n2 = 0;
    t1 = -((dpxy[0].x - x0) * (y1top - y0top) - (dpxy[0].y - y0top) * (x1 - x0));

    for (bssize_t i=0; i<n; i++)
    {
        int j = i + 1;

        if (j >= n)
            j = 0;

        t0 = t1;
        t1 = -((dpxy[j].x - x0) * (y1top - y0top) - (dpxy[j].y - y0top) * (x1 - x0));

        if (t0 >= 0)
            dp2[n2++] = dpxy[i];

        if ((t0 >= 0) != (t1 >= 0) && (t0 <= 0) != (t1 <= 0))
        {
            float const r = t0 / (t0 - t1);
            dp2[n2] = { (dpxy[j].x - dpxy[i].x) * r + dpxy[i].x,
                        (dpxy[j].y - dpxy[i].y) * r + dpxy[i].y };
            n2++;
        }
    }

    if (n2 < 3)
    {
        n = 0;
        return;
    }

    //Clip to (x1,y1bot)-(x0,y0bot)
    t1 = -((dp2[0].x - x1) * (y0bot - y1bot) - (dp2[0].y - y1bot) * (x0 - x1));
    n = 0;

    for (bssize_t i = 0, j = 1; i < n2; j = ++i + 1)
    {
        if (j >= n2)
            j = 0;

        t0 = t1;
        t1 = -((dp2[j].x - x1) * (y0bot - y1bot) - (dp2[j].y - y1bot) * (x0 - x1));

        if (t0 >= 0)
            dpxy[n++] = dp2[i];

        if ((t0 >= 0) != (t1 >= 0) && (t0 <= 0) != (t1 <= 0))
        {
            float const r = t0 / (t0 - t1);
            dpxy[n] = { (dp2[j].x - dp2[i].x) * r + dp2[i].x,
                        (dp2[j].y - dp2[i].y) * r + dp2[i].y };
            n++;
        }
    }

    if (n < 3)
    {
        n = 0;
        return;
    }
}

static void polymost_domost(float x0, float y0, float x1, float y1, float y0top = 0.f, float y0bot = -1.f, float y1top = 0.f, float y1bot = -1.f)
{
    int const dir = (x0 < x1);

    polymost_outputGLDebugMessage(3, "polymost_domost(x0:%f, y0:%f, x1:%f, y1:%f, y0top:%f, y0bot:%f, y1top:%f, y1bot:%f)",
                                  x0, y0, x1, y1, y0top, y0bot, y1top, y1bot);

    y0top -= DOMOST_OFFSET;
    y1top -= DOMOST_OFFSET;
    y0bot += DOMOST_OFFSET;
    y1bot += DOMOST_OFFSET;

    if (dir) //clip dmost (floor)
    {
        y0 -= DOMOST_OFFSET;
        y1 -= DOMOST_OFFSET;
    }
    else //clip umost (ceiling)
    {
        if (x0 == x1) return;
        swapfloat(&x0, &x1);
        swapfloat(&y0, &y1);
        swapfloat(&y0top, &y1top);
        swapfloat(&y0bot, &y1bot);
        y0 += DOMOST_OFFSET;
        y1 += DOMOST_OFFSET; //necessary?
    }

    // Test if span is outside screen bounds
    if (x1 < xbl || x0 > xbr)
    {
        domost_rejectcount++;
        return;
    }

    vec2f_t dm0 = { x0 - DOMOST_OFFSET, y0 };
    vec2f_t dm1 = { x1 + DOMOST_OFFSET, y1 };

    float const slop = (dm1.y - dm0.y) / (dm1.x - dm0.x);

    if (dm0.x < xbl)
    {
        dm0.y += slop*(xbl-dm0.x);
        dm0.x = xbl;
    }

    if (dm1.x > xbr)
    {
        dm1.y += slop*(xbr-dm1.x);
        dm1.x = xbr;
    }

    drawpoly_alpha = 0.f;
    drawpoly_blend = 0;

    vec2f_t n0, n1;
    float spx[4];
    int32_t  spt[4];
    int firstnode = vsp[0].n;

    for (bssize_t newi, i=vsp[0].n; i; i=newi)
    {
        newi = vsp[i].n; n0.x = vsp[i].x; n1.x = vsp[newi].x;

        if (dm0.x >= n1.x)
        {
            firstnode = i;
            continue;
        }
        
        if (n0.x >= dm1.x)
            break;

        if (vsp[i].ctag <= 0) continue;

        float const dx = n1.x-n0.x;
        float const cy[2] = { vsp[i].cy[0], vsp[i].fy[0] },
                    cv[2] = { vsp[i].cy[1]-cy[0], vsp[i].fy[1]-cy[1] };

        int scnt = 0;

        //Test if left edge requires split (dm0.x,dm0.y) (nx0,cy(0)),<dx,cv(0)>
        if ((dm0.x > n0.x) && (dm0.x < n1.x))
        {
            float const t = (dm0.x-n0.x)*cv[dir] - (dm0.y-cy[dir])*dx;
            if (((!dir) && (t < 0.f)) || ((dir) && (t > 0.f)))
                { spx[scnt] = dm0.x; spt[scnt] = -1; scnt++; }
        }

        //Test for intersection on umost (0) and dmost (1)

        float const d[2] = { ((dm0.y - dm1.y) * dx) - ((dm0.x - dm1.x) * cv[0]),
                             ((dm0.y - dm1.y) * dx) - ((dm0.x - dm1.x) * cv[1]) };

        float const n[2] = { ((dm0.y - cy[0]) * dx) - ((dm0.x - n0.x) * cv[0]),
                             ((dm0.y - cy[1]) * dx) - ((dm0.x - n0.x) * cv[1]) };

        float const fnx[2] = { dm0.x + ((n[0] / d[0]) * (dm1.x - dm0.x)),
                               dm0.x + ((n[1] / d[1]) * (dm1.x - dm0.x)) };

        if ((Bfabsf(d[0]) > Bfabsf(n[0])) && (d[0] * n[0] >= 0.f) && (fnx[0] > n0.x) && (fnx[0] < n1.x))
            spx[scnt] = fnx[0], spt[scnt++] = 0;

        if ((Bfabsf(d[1]) > Bfabsf(n[1])) && (d[1] * n[1] >= 0.f) && (fnx[1] > n0.x) && (fnx[1] < n1.x))
            spx[scnt] = fnx[1], spt[scnt++] = 1;

        //Nice hack to avoid full sort later :)
        if ((scnt >= 2) && (spx[scnt-1] < spx[scnt-2]))
        {
            swapfloat(&spx[scnt-1], &spx[scnt-2]);
            swaplong(&spt[scnt-1], &spt[scnt-2]);
        }

        //Test if right edge requires split
        if ((dm1.x > n0.x) && (dm1.x < n1.x))
        {
            float const t = (dm1.x-n0.x)*cv[dir] - (dm1.y-cy[dir])*dx;
            if (((!dir) && (t < 0.f)) || ((dir) && (t > 0.f)))
                { spx[scnt] = dm1.x; spt[scnt] = -1; scnt++; }
        }

        vsp[i].tag = vsp[newi].tag = -1;

        float const rdx = 1.f/dx;

        for (bssize_t z=0, vcnt=0; z<=scnt; z++,i=vcnt)
        {
            float t;

            if (z == scnt)
                goto skip;

            t = (spx[z]-n0.x)*rdx;
            vcnt = vsinsaft(i);
            vsp[i].cy[1] = t*cv[0] + cy[0];
            vsp[i].fy[1] = t*cv[1] + cy[1];
            vsp[vcnt].x = spx[z];
            vsp[vcnt].cy[0] = vsp[i].cy[1];
            vsp[vcnt].fy[0] = vsp[i].fy[1];
            vsp[vcnt].tag = spt[z];

skip: ;
            int32_t const ni = vsp[i].n; if (!ni) continue; //this 'if' fixes many bugs!
            float const dx0 = vsp[i].x; if (dm0.x > dx0) continue;
            float const dx1 = vsp[ni].x; if (dm1.x < dx1) continue;
            n0.y = (dx0-dm0.x)*slop + dm0.y;
            n1.y = (dx1-dm0.x)*slop + dm0.y;

            //      dx0           dx1
            //       ~             ~
            //----------------------------
            //     t0+=0         t1+=0
            //   vsp[i].cy[0]  vsp[i].cy[1]
            //============================
            //     t0+=1         t1+=3
            //============================
            //   vsp[i].fy[0]    vsp[i].fy[1]
            //     t0+=2         t1+=6
            //
            //     ny0 ?         ny1 ?

            int k = 4;

            if ((vsp[i].tag == 0) || (n0.y <= vsp[i].cy[0]+DOMOST_OFFSET)) k--;
            if ((vsp[i].tag == 1) || (n0.y >= vsp[i].fy[0]-DOMOST_OFFSET)) k++;
            if ((vsp[ni].tag == 0) || (n1.y <= vsp[i].cy[1]+DOMOST_OFFSET)) k -= 3;
            if ((vsp[ni].tag == 1) || (n1.y >= vsp[i].fy[1]-DOMOST_OFFSET)) k += 3;

#if 0
            //POGO: This GL1 debug code draws a green line that represents the new line, and the current VSP floor & ceil as red and blue respectively.
            //      To enable this, ensure that in polymost_drawrooms() that you are clearing the stencil buffer and color buffer.
            //      Additionally, disable any calls to glColor4f in polymost_drawpoly and disable culling triangles with area==0/removing duplicate points
            //      If you don't want any lines showing up from mirrors/skyboxes, be sure to disable them as well.
            glEnable(GL_STENCIL_TEST);
            glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glDisable(GL_DEPTH_TEST);
            polymost_useColorOnly(true);
            glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);

            glColor4f(0.f, 1.f, 0.f, 1.f);
            vec2f_t nline[3] = {{dx0, n0.y}, {dx1, n1.y}, {dx0, n0.y}};
            polymost_drawpoly(nline, 3, domostpolymethod);

            glColor4f(1.f, 0.f, 0.f, 1.f);
            vec2f_t floor[3] = {{vsp[i].x, vsp[i].fy[0]}, {vsp[ni].x, vsp[i].fy[1]}, {vsp[i].x, vsp[i].fy[0]}};
            polymost_drawpoly(floor, 3, domostpolymethod);

            glColor4f(0.f, 0.f, 1.f, 1.f);
            vec2f_t ceil[3] = {{vsp[i].x, vsp[i].cy[0]}, {vsp[ni].x, vsp[i].cy[1]}, {vsp[i].x, vsp[i].cy[0]}};
            polymost_drawpoly(ceil, 3, domostpolymethod);

            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            polymost_useColorOnly(false);
            glEnable(GL_DEPTH_TEST);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glStencilFunc(GL_EQUAL, 0, 0xFF);
            glColor4f(1.f, 1.f, 1.f, 1.f);
#endif

            if (!dir)
            {
                switch (k)
                {
                    case 4:
                    case 5:
                    case 7:
                    {
                        vec2f_t dpxy[8] = {
                            { dx0, vsp[i].cy[0] }, { dx1, vsp[i].cy[1] }, { dx1, n1.y }, { dx0, n0.y }
                        };

                        int n = 4;
                        polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                        if (g_nodraw)
                        {
                            if (yax_drawcf != -1)
                                yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, vsp[i].cy[0], vsp[i].cy[1], n0.y, n1.y };

                            if (editstatus && doeditorcheck)
                                polymost_polyeditorfunc(dpxy, n);
                        }
                        else
#endif
                            polymost_drawpoly(dpxy, n, domostpolymethod);

                        vsp[i].cy[0] = n0.y;
                        vsp[i].cy[1] = n1.y;
                        vsp[i].ctag = gtag;
                    }
                    break;
                    case 1:
                    case 2:
                    {
                        vec2f_t dpxy[8] = { { dx0, vsp[i].cy[0] }, { dx1, vsp[i].cy[1] }, { dx0, n0.y } };

                        int n = 3;
                        polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                        if (g_nodraw)
                        {
                            if (yax_drawcf != -1)
                                yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, vsp[i].cy[0], vsp[i].cy[1], n0.y, vsp[i].cy[1] };

                            if (editstatus && doeditorcheck)
                                polymost_polyeditorfunc(dpxy, n);
                        }
                        else
#endif
                            polymost_drawpoly(dpxy, n, domostpolymethod);

                        vsp[i].cy[0] = n0.y;
                        vsp[i].ctag = gtag;
                    }
                    break;
                    case 3:
                    case 6:
                    {
                        vec2f_t dpxy[8] = { { dx0, vsp[i].cy[0] }, { dx1, vsp[i].cy[1] }, { dx1, n1.y } };

                        int n = 3;
                        polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                        if (g_nodraw)
                        {
                            if (yax_drawcf != -1)
                                yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, vsp[i].cy[0], vsp[i].cy[1], vsp[i].cy[0], n1.y };

                            if (editstatus && doeditorcheck)
                                polymost_polyeditorfunc(dpxy, n);
                        }
                        else
#endif
                            polymost_drawpoly(dpxy, n, domostpolymethod);

                        vsp[i].cy[1] = n1.y;
                        vsp[i].ctag = gtag;
                    }
                    break;
                    case 8:
                    {
                        vec2f_t dpxy[8] = {
                            { dx0, vsp[i].cy[0] }, { dx1, vsp[i].cy[1] }, { dx1, vsp[i].fy[1] }, { dx0, vsp[i].fy[0] }
                        };

                        int n = 4;
                        polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                        if (g_nodraw)
                        {
                            if (yax_drawcf != -1)
                                yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, vsp[i].cy[0], vsp[i].cy[1], vsp[i].fy[0], vsp[i].fy[1] };

                            if (editstatus && doeditorcheck)
                                polymost_polyeditorfunc(dpxy, n);
                        }
                        else
#endif
                            polymost_drawpoly(dpxy, n, domostpolymethod);

                        vsp[i].ctag = vsp[i].ftag = -1;
                    }
                    default: break;
                }
            }
            else
            {
                switch (k)
                {
                case 4:
                case 3:
                case 1:
                {
                    vec2f_t dpxy[8] = {
                        { dx0, n0.y }, { dx1, n1.y }, { dx1, vsp[i].fy[1] }, { dx0, vsp[i].fy[0] }
                    };

                    int n = 4;
                    polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                    if (g_nodraw)
                    {
                        if (yax_drawcf != -1)
                            yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, n0.y, n1.y, vsp[i].fy[0], vsp[i].fy[1] };

                        if (editstatus && doeditorcheck)
                            polymost_polyeditorfunc(dpxy, n);
                    }
                    else
#endif
                        polymost_drawpoly(dpxy, n, domostpolymethod);

                    vsp[i].fy[0] = n0.y;
                    vsp[i].fy[1] = n1.y;
                    vsp[i].ftag = gtag;
                }
                    break;
                case 7:
                case 6:
                {
                    vec2f_t dpxy[8] = { { dx0, n0.y }, { dx1, vsp[i].fy[1] }, { dx0, vsp[i].fy[0] } };

                    int n = 3;
                    polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                    if (g_nodraw)
                    {
                        if (yax_drawcf != -1)
                            yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, n0.y, vsp[i].fy[1], vsp[i].fy[0], vsp[i].fy[1] };

                        if (editstatus && doeditorcheck)
                            polymost_polyeditorfunc(dpxy, n);
                    }
                    else
#endif
                        polymost_drawpoly(dpxy, n, domostpolymethod);

                    vsp[i].fy[0] = n0.y;
                    vsp[i].ftag = gtag;
                }
                    break;
                case 5:
                case 2:
                {
                    vec2f_t dpxy[8] = { { dx0, vsp[i].fy[0] }, { dx1, n1.y }, { dx1, vsp[i].fy[1] } };

                    int n = 3;
                    polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                    if (g_nodraw)
                    {
                        if (yax_drawcf != -1)
                            yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, vsp[i].fy[0], n1.y, vsp[i].fy[0], vsp[i].fy[1] };

                        if (editstatus && doeditorcheck)
                            polymost_polyeditorfunc(dpxy, n);
                    }
                    else
#endif
                        polymost_drawpoly(dpxy, n, domostpolymethod);

                    vsp[i].fy[1] = n1.y;
                    vsp[i].ftag = gtag;
                }
                    break;
                case 0:
                {
                    vec2f_t dpxy[8] = { { dx0, vsp[i].cy[0] }, { dx1, vsp[i].cy[1] }, { dx1, vsp[i].fy[1] }, { dx0, vsp[i].fy[0] } };

                    int n = 4;
                    polymost_clipmost(dpxy, n, x0, x1, y0top, y0bot, y1top, y1bot);
#ifdef YAX_ENABLE
                    if (g_nodraw)
                    {
                        if (yax_drawcf != -1)
                            yax_holecf[yax_drawcf][yax_holencf[yax_drawcf]++] = { dx0, dx1, vsp[i].cy[0], vsp[i].cy[1], vsp[i].fy[0], vsp[i].fy[1] };

                        if (editstatus && doeditorcheck)
                            polymost_polyeditorfunc(dpxy, n);
                    }
                    else
#endif
                        polymost_drawpoly(dpxy, n, domostpolymethod);

                    vsp[i].ctag = vsp[i].ftag = -1;
                }
                default:
                    break;
                }
            }
        }
    }

    gtag++;

    //Combine neighboring vertical strips with matching collinear top&bottom edges
    //This prevents x-splits from propagating through the entire scan
#ifdef COMBINE_STRIPS
    int i = firstnode;

    do
    {
        if (vsp[i].x >= dm1.x)
            break;

        if ((vsp[i].cy[0]+DOMOST_OFFSET*2 >= vsp[i].fy[0]) && (vsp[i].cy[1]+DOMOST_OFFSET*2 >= vsp[i].fy[1]))
            vsp[i].ctag = vsp[i].ftag = -1;

        int const ni = vsp[i].n;

        //POGO: specially treat the viewport nodes so that we will never end up in a situation where we accidentally access the sentinel node
        if (ni >= viewportNodeCount)
        {
            if ((vsp[i].ctag == vsp[ni].ctag) && (vsp[i].ftag == vsp[ni].ftag))
            {
                vsmerge(i, ni);
#if 0
                //POGO: This GL1 debug code draws the resulting merged VSP segment with floor and ceiling bounds lines as yellow and cyan respectively
                //      To enable this, ensure that in polymost_drawrooms() that you are clearing the stencil buffer and color buffer.
                //      Additionally, disable any calls to glColor4f in polymost_drawpoly and disable culling triangles with area==0
                //      If you don't want any lines showing up from mirrors/skyboxes, be sure to disable them as well.
                glEnable(GL_STENCIL_TEST);
                glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glDisable(GL_DEPTH_TEST);
                polymost_useColorOnly(true);
                glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);

                glColor4f(1.f, 1.f, 0.f, 1.f);
                vec2f_t dfloor[3] = {{vsp[i].x, vsp[i].fy[0]}, {vsp[vsp[i].n].x, vsp[i].fy[1]}, {vsp[i].x, vsp[i].fy[0]}};
                polymost_drawpoly(dfloor, 3, domostpolymethod);

                glColor4f(0.f, 1.f, 1.f, 1.f);
                vec2f_t dceil[3] = {{vsp[i].x, vsp[i].cy[0]}, {vsp[vsp[i].n].x, vsp[i].cy[1]}, {vsp[i].x, vsp[i].cy[0]}};
                polymost_drawpoly(dceil, 3, domostpolymethod);

                glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
                polymost_useColorOnly(false);
                glEnable(GL_DEPTH_TEST);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glStencilFunc(GL_EQUAL, 0, 0xFF);
                glColor4f(1.f, 1.f, 1.f, 1.f);
#endif
                continue;
            }
            if (vsp[ni].x - vsp[i].x < DOMOST_OFFSET)
            {
                vsp[i].x = vsp[ni].x;
                vsp[i].cy[0] = vsp[ni].cy[0];
                vsp[i].fy[0] = vsp[ni].fy[0];
                vsp[i].ctag = vsp[ni].ctag;
                vsp[i].ftag = vsp[ni].ftag;
                vsmerge(i, ni);
                continue;
            }
        }
        i = ni;
    }
    while (i);
#endif
}

#ifdef YAX_ENABLE
static void yax_polymost_domost(const int yaxbunch, float x0, float y0, float x1, float y1)
{
    int const dir = (x0 < x1);

    if (dir) //clip dmost (floor)
    {
        y0 -= DOMOST_OFFSET;
        y1 -= DOMOST_OFFSET;
    }
    else //clip umost (ceiling)
    {
        if (x0 == x1) return;
        swapfloat(&x0, &x1);
        swapfloat(&y0, &y1);
        y0 += DOMOST_OFFSET;
        y1 += DOMOST_OFFSET; //necessary?
    }

    // Test if span is outside screen bounds
    if (x1 < xbl || x0 > xbr)
    {
        domost_rejectcount++;
        return;
    }

    vec2f_t dm0 = { x0, y0 };
    vec2f_t dm1 = { x1, y1 };

    float const slop = (dm1.y - dm0.y) / (dm1.x - dm0.x);

    if (dm0.x < xbl)
    {
        dm0.y += slop*(xbl-dm0.x);
        dm0.x = xbl;
    }

    if (dm1.x > xbr)
    {
        dm1.y += slop*(xbr-dm1.x);
        dm1.x = xbr;
    }

    vec2f_t n0, n1;
    float spx[4];
    int32_t  spt[4];

    for (bssize_t newi, i=yax_vsp[yaxbunch][0].n; i; i=newi)
    {
        newi = yax_vsp[yaxbunch][i].n; n0.x = yax_vsp[yaxbunch][i].x; n1.x = yax_vsp[yaxbunch][newi].x;

        if ((dm0.x >= n1.x) || (n0.x >= dm1.x) || (yax_vsp[yaxbunch][i].ctag <= 0)) continue;

        double const dx = double(n1.x)-double(n0.x);
        double const cy = yax_vsp[yaxbunch][i].cy[0],
                     cv = yax_vsp[yaxbunch][i].cy[1]-cy;

        int scnt = 0;

        //Test if left edge requires split (dm0.x,dm0.y) (nx0,cy(0)),<dx,cv(0)>
        if ((dm0.x > n0.x) && (dm0.x < n1.x))
        {
            double const t = (dm0.x-n0.x)*cv - (dm0.y-cy)*dx;
            if (((!dir) && (t <= 0.0)) || ((dir) && (t >= 0.0)))
                { spx[scnt] = dm0.x; spt[scnt] = -1; scnt++; }
        }

        //Test for intersection on umost (0) and dmost (1)

        double const d = ((double(dm0.y) - double(dm1.y)) * dx) - ((double(dm0.x) - double(dm1.x)) * cv);

        double const n = ((double(dm0.y) - cy) * dx) - ((double(dm0.x) - double(n0.x)) * cv);

        double const fnx = double(dm0.x) + ((n / d) * (double(dm1.x) - double(dm0.x)));

        if ((fabs(d) > fabs(n)) && (d * n >= 0.0) && (fnx > n0.x) && (fnx < n1.x))
            spx[scnt] = fnx, spt[scnt++] = 0;

        //Nice hack to avoid full sort later :)
        if ((scnt >= 2) && (spx[scnt-1] < spx[scnt-2]))
        {
            swapfloat(&spx[scnt-1], &spx[scnt-2]);
            swaplong(&spt[scnt-1], &spt[scnt-2]);
        }

        //Test if right edge requires split
        if ((dm1.x > n0.x) && (dm1.x < n1.x))
        {
            double const t = (double(dm1.x)- double(n0.x))*cv - (double(dm1.y)- double(cy))*dx;
            if (((!dir) && (t <= 0.0)) || ((dir) && (t >= 0.0)))
                { spx[scnt] = dm1.x; spt[scnt] = -1; scnt++; }
        }

        yax_vsp[yaxbunch][i].tag = yax_vsp[yaxbunch][newi].tag = -1;

        float const rdx = 1.f/dx;

        for (bssize_t z=0, vcnt=0; z<=scnt; z++,i=vcnt)
        {
            float t;

            if (z == scnt)
                goto skip;

            t = (spx[z]-n0.x)*rdx;
            vcnt = yax_vsinsaft(yaxbunch, i);
            yax_vsp[yaxbunch][i].cy[1] = t*cv + cy;
            yax_vsp[yaxbunch][vcnt].x = spx[z];
            yax_vsp[yaxbunch][vcnt].cy[0] = yax_vsp[yaxbunch][i].cy[1];
            yax_vsp[yaxbunch][vcnt].tag = spt[z];

skip: ;
            int32_t const ni = yax_vsp[yaxbunch][i].n; if (!ni) continue; //this 'if' fixes many bugs!
            float const dx0 = yax_vsp[yaxbunch][i].x; if (dm0.x > dx0) continue;
            float const dx1 = yax_vsp[yaxbunch][ni].x; if (dm1.x < dx1) continue;
            n0.y = (dx0-dm0.x)*slop + dm0.y;
            n1.y = (dx1-dm0.x)*slop + dm0.y;

            //      dx0           dx1
            //       ~             ~
            //----------------------------
            //     t0+=0         t1+=0
            //   vsp[i].cy[0]  vsp[i].cy[1]
            //============================
            //     t0+=1         t1+=3
            //============================
            //   vsp[i].fy[0]    vsp[i].fy[1]
            //     t0+=2         t1+=6
            //
            //     ny0 ?         ny1 ?

            int k = 4;

            if (!dir)
            {
                if ((yax_vsp[yaxbunch][i].tag == 0) || (n0.y <= yax_vsp[yaxbunch][i].cy[0]+DOMOST_OFFSET)) k--;
                if ((yax_vsp[yaxbunch][ni].tag == 0) || (n1.y <= yax_vsp[yaxbunch][i].cy[1]+DOMOST_OFFSET)) k -= 3;
                switch (k)
                {
                    case 4:
                    {
                        yax_vsp[yaxbunch][i].cy[0] = n0.y;
                        yax_vsp[yaxbunch][i].cy[1] = n1.y;
                        yax_vsp[yaxbunch][i].ctag = gtag;
                    }
                    break;
                    case 1:
                    case 2:
                    {
                        yax_vsp[yaxbunch][i].cy[0] = n0.y;
                        yax_vsp[yaxbunch][i].ctag = gtag;
                    }
                    break;
                    case 3:
                    {
                        yax_vsp[yaxbunch][i].cy[1] = n1.y;
                        yax_vsp[yaxbunch][i].ctag = gtag;
                    }
                    break;
                    default: break;
                }
            }
            else
            {
                if ((yax_vsp[yaxbunch][i].tag == 0) || (n0.y >= yax_vsp[yaxbunch][i].cy[0]-DOMOST_OFFSET)) k++;
                if ((yax_vsp[yaxbunch][ni].tag == 0) || (n1.y >= yax_vsp[yaxbunch][i].cy[1]-DOMOST_OFFSET)) k += 3;
                switch (k)
                {
                case 4:
                {
                    yax_vsp[yaxbunch][i].cy[0] = n0.y;
                    yax_vsp[yaxbunch][i].cy[1] = n1.y;
                    yax_vsp[yaxbunch][i].ctag = gtag;
                }
                    break;
                case 7:
                case 6:
                {
                    yax_vsp[yaxbunch][i].cy[0] = n0.y;
                    yax_vsp[yaxbunch][i].ctag = gtag;
                }
                    break;
                case 5:
                {
                    yax_vsp[yaxbunch][i].cy[1] = n1.y;
                    yax_vsp[yaxbunch][i].ctag = gtag;
                }
                    break;
                default:
                    break;
                }
            }
        }
    }

    gtag++;

    //Combine neighboring vertical strips with matching collinear top&bottom edges
    //This prevents x-splits from propagating through the entire scan
#ifdef COMBINE_STRIPS
    int i = yax_vsp[yaxbunch][0].n;

    do
    {
        int const ni = yax_vsp[yaxbunch][i].n;

        if ((yax_vsp[yaxbunch][i].ctag == yax_vsp[yaxbunch][ni].ctag))
        {
            yax_vsp[yaxbunch][i].cy[1] = yax_vsp[yaxbunch][ni].cy[1];
            yax_vsdel(yaxbunch, ni);
        }
        else i = ni;
    }
    while (i);
#endif
}

static int32_t should_clip_cfwall(float x0, float y0, float x1, float y1)
{
    int const dir = (x0 < x1);

    if (dir && yax_globallev >= YAX_MAXDRAWS)
        return 1;

    if (!dir && yax_globallev <= YAX_MAXDRAWS)
        return 1;

    if (dir) //clip dmost (floor)
    {
        y0 -= DOMOST_OFFSET;
        y1 -= DOMOST_OFFSET;
    }
    else //clip umost (ceiling)
    {
        if (x0 == x1) return 1;
        swapfloat(&x0, &x1);
        swapfloat(&y0, &y1);
        y0 += DOMOST_OFFSET;
        y1 += DOMOST_OFFSET; //necessary?
    }

    x0 -= DOMOST_OFFSET;
    x1 += DOMOST_OFFSET;

    // Test if span is outside screen bounds
    if (x1 < xbl || x0 > xbr)
        return 1;

    vec2f_t dm0 = { x0, y0 };
    vec2f_t dm1 = { x1, y1 };

    float const slop = (dm1.y - dm0.y) / (dm1.x - dm0.x);

    if (dm0.x < xbl)
    {
        dm0.y += slop*(xbl-dm0.x);
        dm0.x = xbl;
    }

    if (dm1.x > xbr)
    {
        dm1.y += slop*(xbr-dm1.x);
        dm1.x = xbr;
    }

    vec2f_t n0, n1;
    float spx[6], spcy[6], spfy[6];
    int32_t spt[6];

    for (bssize_t newi, i=vsp[0].n; i; i=newi)
    {
        newi = vsp[i].n; n0.x = vsp[i].x; n1.x = vsp[newi].x;

        if ((dm0.x >= n1.x) || (n0.x >= dm1.x) || (vsp[i].ctag <= 0)) continue;

        float const dx = n1.x-n0.x;
        float const cy[2] = { vsp[i].cy[0], vsp[i].fy[0] },
                    cv[2] = { vsp[i].cy[1]-cy[0], vsp[i].fy[1]-cy[1] };

        int scnt = 0;

        spx[scnt] = n0.x; spt[scnt] = -1; scnt++;

        //Test if left edge requires split (dm0.x,dm0.y) (nx0,cy(0)),<dx,cv(0)>
        if ((dm0.x > n0.x) && (dm0.x < n1.x))
        {
            float const t = (dm0.x-n0.x)*cv[dir] - (dm0.y-cy[dir])*dx;
            if (((!dir) && (t < 0.f)) || ((dir) && (t > 0.f)))
                { spx[scnt] = dm0.x; spt[scnt] = -1; scnt++; }
        }

        //Test for intersection on umost (0) and dmost (1)

        float const d[2] = { ((dm0.y - dm1.y) * dx) - ((dm0.x - dm1.x) * cv[0]),
                             ((dm0.y - dm1.y) * dx) - ((dm0.x - dm1.x) * cv[1]) };

        float const n[2] = { ((dm0.y - cy[0]) * dx) - ((dm0.x - n0.x) * cv[0]),
                             ((dm0.y - cy[1]) * dx) - ((dm0.x - n0.x) * cv[1]) };

        float const fnx[2] = { dm0.x + ((n[0] / d[0]) * (dm1.x - dm0.x)),
                               dm0.x + ((n[1] / d[1]) * (dm1.x - dm0.x)) };

        if ((Bfabsf(d[0]) > Bfabsf(n[0])) && (d[0] * n[0] >= 0.f) && (fnx[0] > n0.x) && (fnx[0] < n1.x))
            spx[scnt] = fnx[0], spt[scnt++] = 0;

        if ((Bfabsf(d[1]) > Bfabsf(n[1])) && (d[1] * n[1] >= 0.f) && (fnx[1] > n0.x) && (fnx[1] < n1.x))
            spx[scnt] = fnx[1], spt[scnt++] = 1;

        //Nice hack to avoid full sort later :)
        if ((scnt >= 2) && (spx[scnt-1] < spx[scnt-2]))
        {
            swapfloat(&spx[scnt-1], &spx[scnt-2]);
            swaplong(&spx[scnt-1], &spx[scnt-2]);
        }

        //Test if right edge requires split
        if ((dm1.x > n0.x) && (dm1.x < n1.x))
        {
            float const t = (dm1.x-n0.x)*cv[dir] - (dm1.y-cy[dir])*dx;
            if (((!dir) && (t < 0.f)) || ((dir) && (t > 0.f)))
                { spx[scnt] = dm1.x; spt[scnt] = -1; scnt++; }
        }

        spx[scnt] = n1.x; spt[scnt] = -1; scnt++;

        float const rdx = 1.f/dx;
        for (bssize_t z=0; z<scnt; z++)
        {
            float const t = (spx[z]-n0.x)*rdx;
            spcy[z] = t*cv[0]+cy[0];
            spfy[z] = t*cv[1]+cy[1];
        }

        for (bssize_t z=0; z<scnt-1; z++)
        {
            float const dx0 = spx[z];
            float const dx1 = spx[z+1];
            n0.y = (dx0-dm0.x)*slop + dm0.y;
            n1.y = (dx1-dm0.x)*slop + dm0.y;

            //      dx0           dx1
            //       ~             ~
            //----------------------------
            //     t0+=0         t1+=0
            //   vsp[i].cy[0]  vsp[i].cy[1]
            //============================
            //     t0+=1         t1+=3
            //============================
            //   vsp[i].fy[0]    vsp[i].fy[1]
            //     t0+=2         t1+=6
            //
            //     ny0 ?         ny1 ?

            int k = 4;
            if (dir)
            {
                if ((spt[z] == 0) || (n0.y <= spcy[z]+DOMOST_OFFSET)) k--;
                if ((spt[z+1] == 0) || (n1.y <= spcy[z+1]+DOMOST_OFFSET)) k -= 3;
                if (k != 0)
                    return 1;
            }
            else
            {
                if ((spt[z] == 1) || (n0.y >= spfy[z]-DOMOST_OFFSET)) k++;
                if ((spt[z+1] == 1) || (n1.y >= spfy[z+1]-DOMOST_OFFSET)) k += 3;
                if (k != 8)
                    return 1;
            }
        }
    }
    return 0;
}

#endif

void polymost_editorfunc(void)
{
    const float ratio = r_usenewaspect ? (fxdim / fydim) / (320.f / 240.f) : 1.f;

    vec3f_t tvect = { 1.f,
                      (fsearchx-ghalfx)/ghalfx*ratio,
                      ((fsearchy-ghoriz)*16.f*(240.f/320.f))/ghoriz };

    //Standard Left/right rotation
    vec3_t v = { Blrintf(tvect.x * fcosglobalang - tvect.y * fsinglobalang),
                 Blrintf(tvect.x * fsinglobalang + tvect.y * fcosglobalang), Blrintf(tvect.z * 16384.f) };

    vec3_t vect = { globalposx, globalposy, globalposz };

    hitdata_t *hit = &polymost_hitdata;

    hitallsprites = 1;

    hitscan((const vec3_t *) &vect, globalcursectnum, //Start position
        v.x, v.y, v.z, hit, 0xffff0030);

    hitallsprites = 0;
}


// variables that are set to ceiling- or floor-members, depending
// on which one is processed right now
static int32_t global_cf_z;
static float global_cf_xpanning, global_cf_ypanning, global_cf_heinum;
static int32_t global_cf_shade, global_cf_pal, global_cf_fogpal;
static float (*global_getzofslope_func)(usectorptr_t, float, float);

static void polymost_internal_nonparallaxed(vec2f_t n0, vec2f_t n1, float ryp0, float ryp1, float x0, float x1,
                                            float y0, float y1, int32_t sectnum)
{
    int const have_floor = sectnum & MAXSECTORS;
    sectnum &= ~MAXSECTORS;
    auto const sec = (usectorptr_t)&sector[sectnum];

    // comments from floor code:
            //(singlobalang/-16384*(sx-ghalfx) + 0*(sy-ghoriz) + (cosviewingrangeglobalang/16384)*ghalfx)*d + globalposx    = u*16
            //(cosglobalang/ 16384*(sx-ghalfx) + 0*(sy-ghoriz) + (sinviewingrangeglobalang/16384)*ghalfx)*d + globalposy    = v*16
            //(                  0*(sx-ghalfx) + 1*(sy-ghoriz) + (                             0)*ghalfx)*d + globalposz/16 = (sec->floorz/16)

    float ft[4] = { fglobalposx, fglobalposy, fcosglobalang, fsinglobalang };

    polymost_outputGLDebugMessage(3, "polymost_internal_nonparallaxed(n0:{x:%f, y:%f}, n1:{x:%f, y:%f}, ryp0:%f, ryp1:%f, x0:%f, x1:%f, y0:%f, y1:%f, sectnum:%d)",
                                  n0.x, n0.y, n1.x, n1.y, ryp0, ryp1, x0, x1, y0, y1, sectnum);

    if (globalorientation & 64)
    {
        //relative alignment
        vec2_t const xy = { wall[wall[sec->wallptr].point2].x - wall[sec->wallptr].x,
                            wall[wall[sec->wallptr].point2].y - wall[sec->wallptr].y };
        float r;

        if (globalorientation & 2)
        {
            int i = krecipasm(nsqrtasm(uhypsq(xy.x,xy.y)));
            r = i * (1.f/1073741824.f);
        }
        else
        {
            int i = nsqrtasm(uhypsq(xy.x,xy.y)); if (i == 0) i = 1024; else i = tabledivide32(1048576, i);
            r = i * (1.f/1048576.f);
        }

        vec2f_t const fxy = { xy.x*r, xy.y*r };

        ft[0] = ((float)(globalposx - wall[sec->wallptr].x)) * fxy.x + ((float)(globalposy - wall[sec->wallptr].y)) * fxy.y;
        ft[1] = ((float)(globalposy - wall[sec->wallptr].y)) * fxy.x - ((float)(globalposx - wall[sec->wallptr].x)) * fxy.y;
        ft[2] = fcosglobalang * fxy.x + fsinglobalang * fxy.y;
        ft[3] = fsinglobalang * fxy.x - fcosglobalang * fxy.y;

        globalorientation ^= (!(globalorientation & 4)) ? 32 : 16;
    }

    xtex.d = 0;
    ytex.d = gxyaspect;

    if (!(globalorientation&2) && global_cf_z-globalposz)  // PK 2012: don't allow div by zero
            ytex.d /= (double)(global_cf_z-globalposz);

    otex.d = -ghoriz * ytex.d;

    if (globalorientation & 8)
    {
        ft[0] *=  (1.f / 8.f);
        ft[1] *= -(1.f / 8.f);
        ft[2] *=  (1.f / 2097152.f);
        ft[3] *=  (1.f / 2097152.f);
    }
    else
    {
        ft[0] *=  (1.f / 16.f);
        ft[1] *= -(1.f / 16.f);
        ft[2] *=  (1.f / 4194304.f);
        ft[3] *=  (1.f / 4194304.f);
    }

    xtex.u = ft[3] * -(1.f / 65536.f) * (double)viewingrange;
    xtex.v = ft[2] * -(1.f / 65536.f) * (double)viewingrange;
    ytex.u = ft[0] * ytex.d;
    ytex.v = ft[1] * ytex.d;
    otex.u = ft[0] * otex.d;
    otex.v = ft[1] * otex.d;
    otex.u += (ft[2] - xtex.u) * ghalfx;
    otex.v -= (ft[3] + xtex.v) * ghalfx;

    //Texture flipping
    if (globalorientation&4)
    {
        swapdouble(&xtex.u, &xtex.v);
        swapdouble(&ytex.u, &ytex.v);
        swapdouble(&otex.u, &otex.v);
    }

    if (globalorientation&16) { xtex.u = -xtex.u; ytex.u = -ytex.u; otex.u = -otex.u; }
    if (globalorientation&32) { xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; }

    //Texture panning
    vec2f_t fxy = { global_cf_xpanning * ((float)(1 << (picsiz[globalpicnum] & 15))) * (1.0f / 256.f),
                    global_cf_ypanning * ((float)(1 << (picsiz[globalpicnum] >> 4))) * (1.0f / 256.f) };

    if ((globalorientation&(2+64)) == (2+64)) //Hack for panning for slopes w/ relative alignment
    {
        float r = global_cf_heinum * (1.0f / 4096.f);
        r = polymost_invsqrt_approximation(r * r + 1);

        if (!(globalorientation & 4))
            fxy.y *= r;
        else
            fxy.x *= r;
    }
    ytex.u += ytex.d*fxy.x; otex.u += otex.d*fxy.x;
    ytex.v += ytex.d*fxy.y; otex.v += otex.d*fxy.y;

    if (globalorientation&2) //slopes
    {
        //Pick some point guaranteed to be not collinear to the 1st two points
        vec2f_t dxy = { n1.y - n0.y, n0.x - n1.x };

        float const dxyr = polymost_invsqrt_approximation(dxy.x * dxy.x + dxy.y * dxy.y);

        dxy.x *= dxyr * 4096.f;
        dxy.y *= dxyr * 4096.f;

        vec2f_t const oxy = { n0.x + dxy.x, n0.y + dxy.y };

        float const ox2 = (oxy.y - fglobalposy) * gcosang - (oxy.x - fglobalposx) * gsinang;
        float oy2 = 1.f / ((oxy.x - fglobalposx) * gcosang2 + (oxy.y - fglobalposy) * gsinang2);

        double const px[3] = { x0, x1, (double)ghalfx * ox2 * oy2 + ghalfx };

        oy2 *= gyxscale;

        double py[3] = { ryp0 + (double)ghoriz, ryp1 + (double)ghoriz, oy2 + (double)ghoriz };

        vec3d_t const duv[3] = {
            { (px[0] * xtex.d + py[0] * ytex.d + otex.d),
              (px[0] * xtex.u + py[0] * ytex.u + otex.u),
              (px[0] * xtex.v + py[0] * ytex.v + otex.v)
            },
            { (px[1] * xtex.d + py[1] * ytex.d + otex.d),
              (px[1] * xtex.u + py[1] * ytex.u + otex.u),
              (px[1] * xtex.v + py[1] * ytex.v + otex.v)
            },
            { (px[2] * xtex.d + py[2] * ytex.d + otex.d),
              (px[2] * xtex.u + py[2] * ytex.u + otex.u),
              (px[2] * xtex.v + py[2] * ytex.v + otex.v)
            }
        };

        py[0] = y0;
        py[1] = y1;
        py[2] = double(global_getzofslope_func((usectorptr_t)&sector[sectnum], oxy.x, oxy.y) - globalposz) * oy2 + ghoriz;

        vec3f_t oxyz[2] = { { (float)(py[1] - py[2]), (float)(py[2] - py[0]), (float)(py[0] - py[1]) },
                            { (float)(px[2] - px[1]), (float)(px[0] - px[2]), (float)(px[1] - px[0]) } };

        float const r = 1.f / (oxyz[0].x * px[0] + oxyz[0].y * px[1] + oxyz[0].z * px[2]);

        xtex.d = (oxyz[0].x * duv[0].d + oxyz[0].y * duv[1].d + oxyz[0].z * duv[2].d) * r;
        xtex.u = (oxyz[0].x * duv[0].u + oxyz[0].y * duv[1].u + oxyz[0].z * duv[2].u) * r;
        xtex.v = (oxyz[0].x * duv[0].v + oxyz[0].y * duv[1].v + oxyz[0].z * duv[2].v) * r;

        ytex.d = (oxyz[1].x * duv[0].d + oxyz[1].y * duv[1].d + oxyz[1].z * duv[2].d) * r;
        ytex.u = (oxyz[1].x * duv[0].u + oxyz[1].y * duv[1].u + oxyz[1].z * duv[2].u) * r;
        ytex.v = (oxyz[1].x * duv[0].v + oxyz[1].y * duv[1].v + oxyz[1].z * duv[2].v) * r;

        otex.d = duv[0].d - px[0] * xtex.d - py[0] * ytex.d;
        otex.u = duv[0].u - px[0] * xtex.u - py[0] * ytex.u;
        otex.v = duv[0].v - px[0] * xtex.v - py[0] * ytex.v;

        if (globalorientation&64) //Hack for relative alignment on slopes
        {
            float r = global_cf_heinum * (1.0f / 4096.f);
            r = Bsqrtf(r*r+1);
            if (!(globalorientation&4)) { xtex.v *= r; ytex.v *= r; otex.v *= r; }
            else { xtex.u *= r; ytex.u *= r; otex.u *= r; }
        }
    }

    domostpolymethod = (globalorientation>>7) & DAMETH_MASKPROPS;

    pow2xsplit = 0;
    drawpoly_alpha = 0.f;
    drawpoly_blend = 0;

    if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
        calc_and_apply_fog(fogshade(global_cf_shade, global_cf_pal), sec->visibility, POLYMOST_CHOOSE_FOG_PAL(global_cf_fogpal, global_cf_pal));

    if (have_floor)
    {
        if (globalposz > getflorzofslope(sectnum, globalposx, globalposy))
            domostpolymethod = DAMETH_BACKFACECULL; //Back-face culling

        if (domostpolymethod & DAMETH_MASKPROPS)
            glEnable(GL_BLEND);

        polymost_domost(x0, y0, x1, y1); //flor
    }
    else
    {
        if (globalposz < getceilzofslope(sectnum, globalposx, globalposy))
            domostpolymethod = DAMETH_BACKFACECULL; //Back-face culling

        if (domostpolymethod & DAMETH_MASKPROPS)
            glEnable(GL_BLEND);

        polymost_domost(x1, y1, x0, y0); //ceil
    }

    if (domostpolymethod & DAMETH_MASKPROPS)
        glDisable(GL_BLEND);

    domostpolymethod = DAMETH_NOMASK;
}

static void calc_ypanning(int32_t refposz, float ryp0, float ryp1,
                          float x0, float x1, uint8_t ypan, uint8_t yrepeat,
                          int32_t dopancor)
{
    float const t0 = ((float)(refposz-globalposz))*ryp0 + ghoriz;
    float const t1 = ((float)(refposz-globalposz))*ryp1 + ghoriz;
    float t = (float(xtex.d*x0 + otex.d) * (float)yrepeat) / ((x1-x0) * ryp0 * 2048.f);
    int i = (1<<(picsiz[globalpicnum]>>4));
    if (i < tilesiz[globalpicnum].y) i <<= 1;

#ifdef NEW_MAP_FORMAT
    if (g_loadedMapVersion >= 10)
        i = tilesiz[globalpicnum].y;
    else
#endif
    if (polymost_is_npotmode())
    {
        t *= (float)tilesiz[globalpicnum].y / i;
        i = tilesiz[globalpicnum].y;
    }
    else if (!(glinfo.texnpot && r_npotwallmode == 2) && dopancor)
    {
        // Carry out panning "correction" to make it look like classic in some
        // cases, but failing in the general case.
        int32_t yoffs = Blrintf((i-tilesiz[globalpicnum].y)*(255.f/i));

        if (ypan > 256-yoffs)
            ypan -= yoffs;
    }

    float const fy = (float)(ypan * i) * (1.f / 256.f);

    xtex.v = double(t0 - t1) * t;
    ytex.v = double(x1 - x0) * t;
    otex.v = -xtex.v * x0 - ytex.v * t0 + fy * otex.d;
    xtex.v += fy * xtex.d;
    ytex.v += fy * ytex.d;
}

static inline int32_t testvisiblemost(float const x0, float const x1)
{
    for (bssize_t i=vsp[0].n, newi; i; i=newi)
    {
        newi = vsp[i].n;
        if ((x0 < vsp[newi].x) && (vsp[i].x < x1) && (vsp[i].ctag >= 0))
            return 1;
    }
    return 0;
}

static inline int polymost_getclosestpointonwall(vec2_t const * const pos, int32_t dawall, vec2_t * const n)
{
    vec2_t const w = { wall[dawall].x, wall[dawall].y };
    vec2_t const d = { POINT2(dawall).x - w.x, POINT2(dawall).y - w.y };
    int64_t i = d.x * ((int64_t)pos->x - w.x) + d.y * ((int64_t)pos->y - w.y);

    if (i < 0)
        return 1;

    int64_t const j = (int64_t)d.x * d.x + (int64_t)d.y * d.y;

    if (i > j)
        return 1;

    i = tabledivide64((i << 15), j) << 15;

    n->x = w.x + ((d.x * i) >> 30);
    n->y = w.y + ((d.y * i) >> 30);

    return 0;
}

float fgetceilzofslope(usectorptr_t sec, float dax, float day)
{
    if (!(sec->ceilingstat&2))
        return float(sec->ceilingz);

    auto const wal  = (uwallptr_t)&wall[sec->wallptr];
    auto const wal2 = (uwallptr_t)&wall[wal->point2];

    vec2_t const w = *(vec2_t const *)wal;
    vec2_t const d = { wal2->x - w.x, wal2->y - w.y };

    int const i = nsqrtasm(uhypsq(d.x,d.y))<<5;
    if (i == 0) return sec->ceilingz;

    float const j = (d.x*(day-w.y)-d.y*(dax-w.x))*(1.f/8.f);
    return float(sec->ceilingz) + (sec->ceilingheinum*j)/i;
}

float fgetflorzofslope(usectorptr_t sec, float dax, float day)
{
    if (!(sec->floorstat&2))
        return float(sec->floorz);

    auto const wal  = (uwallptr_t)&wall[sec->wallptr];
    auto const wal2 = (uwallptr_t)&wall[wal->point2];

    vec2_t const w = *(vec2_t const *)wal;
    vec2_t const d = { wal2->x - w.x, wal2->y - w.y };

    int const i = nsqrtasm(uhypsq(d.x,d.y))<<5;
    if (i == 0) return sec->floorz;

    float const j = (d.x*(day-w.y)-d.y*(dax-w.x))*(1.f/8.f);
    return float(sec->floorz) + (sec->floorheinum*j)/i;
}

void fgetzsofslope(usectorptr_t sec, float dax, float day, float* ceilz, float *florz)
{
    *ceilz = float(sec->ceilingz); *florz = float(sec->floorz);

    if (((sec->ceilingstat|sec->floorstat)&2) != 2)
        return;

    auto const wal  = (uwallptr_t)&wall[sec->wallptr];
    auto const wal2 = (uwallptr_t)&wall[wal->point2];

    vec2_t const d = { wal2->x - wal->x, wal2->y - wal->y };

    int const i = nsqrtasm(uhypsq(d.x,d.y))<<5;
    if (i == 0) return;
    
    float const j = (d.x*(day-wal->y)-d.y*(dax-wal->x))*(1.f/8.f);
    if (sec->ceilingstat&2)
        *ceilz += (sec->ceilingheinum*j)/i;
    if (sec->floorstat&2)
        *florz += (sec->floorheinum*j)/i;
}

static void polymost_flatskyrender(vec2f_t const* const dpxy, int32_t const n, int32_t method)
{
    flatskyrender = 0;
    vec2f_t xys[8];

    // Transform polygon to sky coordinates
    for (int i = 0; i < n; i++)
    {
        vec3f_t const o = { dpxy[i].x-ghalfx, dpxy[i].y-ghalfy, ghalfx / gvrcorrection };

        //Up/down rotation
        vec3d_t v = { o.x, o.y * gchang - o.z * gshang, o.z * gchang + o.y * gshang };
        float const r = (ghalfx / gvrcorrection) / v.z;
        xys[i].x = v.x * r + ghalfx;
        xys[i].y = v.y * r + ghalfy;
    }
    
    float const fglobalang = fix16_to_float(qglobalang);
    int32_t dapyscale, dapskybits, dapyoffs, daptileyscale;
    int8_t const * dapskyoff = getpsky(globalpicnum, &dapyscale, &dapskybits, &dapyoffs, &daptileyscale);

    ghoriz = (qglobalhoriz*(1.f/65536.f)-float(ydimen>>1))*dapyscale*(1.f/65536.f)+float(ydimen>>1)+ghorizcorrect;

    float const dd = fxdimen*.0000001f; //Adjust sky depth based on screen size!
    float vv[2];
    float t = (float)((1<<(picsiz[globalpicnum]&15))<<dapskybits);
    vv[1] = dd*((float)xdimscale*fviewingrange) * (1.f/(daptileyscale*65536.f));
    vv[0] = dd*((float)((tilesiz[globalpicnum].y>>1)+dapyoffs)) - vv[1]*ghoriz;
    int ti = (1<<(picsiz[globalpicnum]>>4)); if (ti != tilesiz[globalpicnum].y) ti += ti;
    vec3f_t o;

    skyclamphack = 0;

    xtex.d = xtex.v = 0;
    ytex.d = ytex.u = 0;
    otex.d = dd;
    xtex.u = otex.d * (t * double(((uint64_t)xdimscale * yxaspect) * viewingrange)) *
                        (1.0 / (16384.0 * 65536.0 * 65536.0 * 5.0 * 1024.0));
    ytex.v = vv[1];
    otex.v = r_parallaxskypanning ? vv[0] + dd*(float)global_cf_ypanning*(float)ti*(1.f/256.f) : vv[0];

    float x0 = xys[0].x, x1 = xys[0].x;

    for (bssize_t i=n-1; i>=1; i--)
    {
        if (xys[i].x < x0) x0 = xys[i].x;
        if (xys[i].x > x1) x1 = xys[i].x;
    }

    int const npot = (1<<(picsiz[globalpicnum]&15)) != tilesiz[globalpicnum].x;
    int const xpanning = (r_parallaxskypanning?global_cf_xpanning:0);

    polymost_setClamp((npot || xpanning != 0) ? 0 : 2);

    int picnumbak = globalpicnum;
    ti = globalpicnum;
    o.y = fviewingrange/(ghalfx*256.f); o.z = 1.f/o.y;

    int y = ((int32_t)(((x0-ghalfx)*o.y)+fglobalang)>>(11-dapskybits));
    float fx = x0;

    do
    {
        globalpicnum = dapskyoff[y&((1<<dapskybits)-1)]+ti;
        if (npot)
        {
            float fx = ((float)((y<<(11-dapskybits))-fglobalang))*o.z+ghalfx;
            int tang = (y<<(11-dapskybits))&2047;
            otex.u = otex.d*(t*((float)(tang)) * (1.f/2048.f) + xpanning) - xtex.u*fx;
        }
        else
            otex.u = otex.d*(t*((float)(fglobalang-(y<<(11-dapskybits)))) * (1.f/2048.f) + xpanning) - xtex.u*ghalfx;
        y++;
        o.x = fx; fx = ((float)((y<<(11-dapskybits))-fglobalang))*o.z+ghalfx;

        if (fx > x1) { fx = x1; ti = -1; }

        vec3d_t otexbak = otex, xtexbak = xtex, ytexbak = ytex;

        // Transform texture mapping factors
        vec2f_t fxy[3] = { { ghalfx * (1.f - 0.25f), ghalfy * (1.f - 0.25f) },
                          { ghalfx, ghalfy * (1.f + 0.25f) },
                          { ghalfx * (1.f + 0.25f), ghalfy * (1.f - 0.25f) } };

        vec3d_t duv[3] = {
            { (fxy[0].x * xtex.d + fxy[0].y * ytex.d + otex.d),
              (fxy[0].x * xtex.u + fxy[0].y * ytex.u + otex.u),
              (fxy[0].x * xtex.v + fxy[0].y * ytex.v + otex.v)
            },
            { (fxy[1].x * xtex.d + fxy[1].y * ytex.d + otex.d),
              (fxy[1].x * xtex.u + fxy[1].y * ytex.u + otex.u),
              (fxy[1].x * xtex.v + fxy[1].y * ytex.v + otex.v)
            },
            { (fxy[2].x * xtex.d + fxy[2].y * ytex.d + otex.d),
              (fxy[2].x * xtex.u + fxy[2].y * ytex.u + otex.u),
              (fxy[2].x * xtex.v + fxy[2].y * ytex.v + otex.v)
            }
        };
        vec2f_t fxyt[3];
        vec3d_t duvt[3];

        for (int i = 0; i < 3; i++)
        {
            vec2f_t const o = { fxy[i].x-ghalfx, fxy[i].y-ghalfy };
            vec3f_t const o2 = { o.x, o.y, ghalfx / gvrcorrection };

            //Up/down rotation (backwards)
            vec3d_t v = { o2.x, o2.y * gchang + o2.z * gshang, o2.z * gchang - o2.y * gshang };
            float const r = (ghalfx / gvrcorrection) / v.z;
            fxyt[i].x = v.x * r + ghalfx;
            fxyt[i].y = v.y * r + ghalfy;
            duvt[i].d = duv[i].d*r;
            duvt[i].u = duv[i].u*r;
            duvt[i].v = duv[i].v*r;
        }

        vec3f_t oxyz[2] = { { (float)(fxyt[1].y - fxyt[2].y), (float)(fxyt[2].y - fxyt[0].y), (float)(fxyt[0].y - fxyt[1].y) },
                            { (float)(fxyt[2].x - fxyt[1].x), (float)(fxyt[0].x - fxyt[2].x), (float)(fxyt[1].x - fxyt[0].x) } };

        float const rr = 1.f / (oxyz[0].x * fxyt[0].x + oxyz[0].y * fxyt[1].x + oxyz[0].z * fxyt[2].x);

        xtex.d = (oxyz[0].x * duvt[0].d + oxyz[0].y * duvt[1].d + oxyz[0].z * duvt[2].d) * rr;
        xtex.u = (oxyz[0].x * duvt[0].u + oxyz[0].y * duvt[1].u + oxyz[0].z * duvt[2].u) * rr;
        xtex.v = (oxyz[0].x * duvt[0].v + oxyz[0].y * duvt[1].v + oxyz[0].z * duvt[2].v) * rr;

        ytex.d = (oxyz[1].x * duvt[0].d + oxyz[1].y * duvt[1].d + oxyz[1].z * duvt[2].d) * rr;
        ytex.u = (oxyz[1].x * duvt[0].u + oxyz[1].y * duvt[1].u + oxyz[1].z * duvt[2].u) * rr;
        ytex.v = (oxyz[1].x * duvt[0].v + oxyz[1].y * duvt[1].v + oxyz[1].z * duvt[2].v) * rr;

        otex.d = duvt[0].d - fxyt[0].x * xtex.d - fxyt[0].y * ytex.d;
        otex.u = duvt[0].u - fxyt[0].x * xtex.u - fxyt[0].y * ytex.u;
        otex.v = duvt[0].v - fxyt[0].x * xtex.v - fxyt[0].y * ytex.v;

        vec2f_t cxy[8];
        vec2f_t cxy2[8];
        int n2 = 0, n3 = 0;

        // Clip to o.x
        for (bssize_t i=0; i<n; i++)
        {
            int const j = i < n-1 ? i + 1 : 0;

            if (xys[i].x >= o.x)
                cxy[n2++] = xys[i];

            if ((xys[i].x >= o.x) != (xys[j].x >= o.x))
            {
                float const r = (o.x - xys[i].x) / (xys[j].x - xys[i].x);
                cxy[n2++] = { o.x, (xys[j].y - xys[i].y) * r + xys[i].y };
            }
        }

        // Clip to fx
        for (bssize_t i=0; i<n2; i++)
        {
            int const j = i < n2-1 ? i + 1 : 0;

            if (cxy[i].x <= fx)
                cxy2[n3++] = cxy[i];

            if ((cxy[i].x <= fx) != (cxy[j].x <= fx))
            {
                float const r = (fx - cxy[i].x) / (cxy[j].x - cxy[i].x);
                cxy2[n3++] = { fx, (cxy[j].y - cxy[i].y) * r + cxy[i].y };
            }
        }

        // Transform back to polymost coordinates
        for (int i = 0; i < n3; i++)
        {
            vec3f_t const o = { cxy2[i].x-ghalfx, cxy2[i].y-ghalfy, ghalfx / gvrcorrection };

            //Up/down rotation
            vec3d_t v = { o.x, o.y * gchang + o.z * gshang, o.z * gchang - o.y * gshang };
            float const r = (ghalfx / gvrcorrection) / v.z;
            cxy[i].x = v.x * r + ghalfx;
            cxy[i].y = v.y * r + ghalfy;
        }

        polymost_drawpoly(cxy, n3, method|DAMETH_WALL);

        otex = otexbak, xtex = xtexbak, ytex = ytexbak;
    }
    while (ti >= 0);

    globalpicnum = picnumbak;

    polymost_setClamp(0);

    flatskyrender = 1;
}

static void polymost_drawalls(int32_t const bunch)
{
    drawpoly_alpha = 0.f;
    drawpoly_blend = 0;

    int32_t const sectnum = thesector[bunchfirst[bunch]];
    auto const sec = (usectorptr_t)&sector[sectnum];
    float const fglobalang = fix16_to_float(qglobalang);

    polymost_outputGLDebugMessage(3, "polymost_drawalls(bunch:%d)", bunch);

    //DRAW WALLS SECTION!
    for (bssize_t z=bunchfirst[bunch]; z>=0; z=bunchp2[z])
    {
        int32_t const wallnum = thewall[z];

        auto const wal = (uwallptr_t)&wall[wallnum];
        auto const wal2 = (uwallptr_t)&wall[wal->point2];
        int32_t const nextsectnum = wal->nextsector;
        auto const nextsec = nextsectnum>=0 ? (usectorptr_t)&sector[nextsectnum] : NULL;

        //Offset&Rotate 3D coordinates to screen 3D space
        vec2f_t walpos = { (float)(wal->x-globalposx), (float)(wal->y-globalposy) };

        vec2f_t p0 = { walpos.y * gcosang - walpos.x * gsinang, walpos.x * gcosang2 + walpos.y * gsinang2 };
        vec2f_t const op0 = p0;

        walpos = { (float)(wal2->x - globalposx),
                   (float)(wal2->y - globalposy) };

        vec2f_t p1 = { walpos.y * gcosang - walpos.x * gsinang, walpos.x * gcosang2 + walpos.y * gsinang2 };

        //Clip to close parallel-screen plane

        vec2f_t n0, n1;
        float t0, t1;

        if (p0.y < SCISDIST)
        {
            if (p1.y < SCISDIST) continue;
            t0 = (SCISDIST-p0.y)/(p1.y-p0.y);
            p0 = { (p1.x-p0.x)*t0+p0.x, SCISDIST };
            n0 = { (wal2->x-wal->x)*t0+wal->x,
                   (wal2->y-wal->y)*t0+wal->y };
        }
        else
        {
            t0 = 0.f;
            n0 = { (float)wal->x, (float)wal->y };
        }

        if (p1.y < SCISDIST)
        {
            t1 = (SCISDIST-op0.y)/(p1.y-op0.y);
            p1 = { (p1.x-op0.x)*t1+op0.x, SCISDIST };
            n1 = { (wal2->x-wal->x)*t1+wal->x,
                   (wal2->y-wal->y)*t1+wal->y };
        }
        else
        {
            t1 = 1.f;
            n1 = { (float)wal2->x, (float)wal2->y };
        }

        float ryp0 = 1.f/p0.y, ryp1 = 1.f/p1.y;

        //Generate screen coordinates for front side of wall
        float const x0 = ghalfx*p0.x*ryp0 + ghalfx, x1 = ghalfx*p1.x*ryp1 + ghalfx;

        if (x1 <= x0) continue;

        ryp0 *= gyxscale; ryp1 *= gyxscale;

        float cz, fz;

        fgetzsofslope((usectorptr_t)&sector[sectnum],n0.x,n0.y,&cz,&fz);
        float const cy0 = (cz-globalposz)*ryp0 + ghoriz, fy0 = (fz-globalposz)*ryp0 + ghoriz;

        fgetzsofslope((usectorptr_t)&sector[sectnum],n1.x,n1.y,&cz,&fz);
        float const cy1 = (cz-globalposz)*ryp1 + ghoriz, fy1 = (fz-globalposz)*ryp1 + ghoriz;

        xtex2.d = (ryp0 - ryp1)*gxyaspect / (x0 - x1);
        ytex2.d = 0;
        otex2.d = ryp0 * gxyaspect - xtex2.d*x0;

        xtex2.u = ytex2.u = otex2.u = 0;
        xtex2.v = ytex2.v = otex2.v = 0;
        
        // Floor

        if (searchit == 2
#ifdef YAX_ENABLE
            && (yax_getbunch(sectnum, YAX_FLOOR) < 0 || showinvisibility || (sec->floorstat&(256+128)) || klabs(yax_globallev-YAX_MAXDRAWS)==YAX_MAXDRAWS)
#endif
            )
        {
            psectnum = sectnum;
            pwallnum = wallnum;
            psearchstat = 2;
            doeditorcheck = 1;
        }

#ifdef YAX_ENABLE
        yax_holencf[YAX_FLOOR] = 0;
        yax_drawcf = YAX_FLOOR;
#endif

        globalpicnum = sec->floorpicnum;
        globalshade = sec->floorshade;
        globalpal = sec->floorpal;
        globalorientation = sec->floorstat;
        globvis = (sector[sectnum].visibility != 0) ?
                  mulscale4(globalcisibility, (uint8_t)(sector[sectnum].visibility + 16)) :
                  globalcisibility;
        globvis2 = (sector[sectnum].visibility != 0) ?
                  mulscale4(globalcisibility2, (uint8_t)(sector[sectnum].visibility + 16)) :
                  globalcisibility2;
        polymost_setVisibility(globvis2);

        tileUpdatePicnum(&globalpicnum, sectnum);

        int32_t dapyscale, dapskybits, dapyoffs, daptileyscale;
        int8_t const * dapskyoff = getpsky(globalpicnum, &dapyscale, &dapskybits, &dapyoffs, &daptileyscale);

        global_cf_fogpal = sec->fogpal;
        global_cf_shade = sec->floorshade, global_cf_pal = sec->floorpal; global_cf_z = sec->floorz;  // REFACT
        global_cf_xpanning = sec->floorxpanning; global_cf_ypanning = sec->floorypanning, global_cf_heinum = sec->floorheinum;
        global_getzofslope_func = &fgetflorzofslope;

        if (!(globalorientation&1))
        {
            int32_t fz = getflorzofslope(sectnum, globalposx, globalposy);
            if (globalposz <= fz)
                polymost_internal_nonparallaxed(n0, n1, ryp0, ryp1, x0, x1, fy0, fy1, sectnum | MAXSECTORS);
        }
        else if ((nextsectnum < 0) || (!(sector[nextsectnum].floorstat&1)))
        {
            //Parallaxing sky... hacked for Ken's mountain texture
            if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
                calc_and_apply_fog_factor(sec->floorshade, sec->visibility, sec->floorpal, 0.005f);

            globvis2 = globalpisibility;
            if (sec->visibility != 0)
                globvis2 = mulscale4(globvis2, (uint8_t)(sec->visibility + 16));
            float viscale = xdimscale*fxdimen*(.0000001f/256.f);
            polymost_setVisibility(globvis2*viscale);

            //Use clamping for tiled sky textures
            //(don't wrap around edges if the sky use multiple panels)
            for (bssize_t i=(1<<dapskybits)-1; i>0; i--)
                if (dapskyoff[i] != dapskyoff[i-1])
                    { skyclamphack = r_parallaxskyclamping; break; }

            skyzbufferhack = 1;

            if (!usehightile || !hicfindskybox(globalpicnum, globalpal))
            {
                float const ghorizbak = ghoriz;
                if (r_flatsky && ! r_yshearing)
                {
                    pow2xsplit = 0;
                    skyclamphack = 0;
                    flatskyrender = 1;
                    globalshade += globvis2*xdimscale*fviewingrange*(1.f / (64.f * 65536.f * 256.f * 1024.f));
                    polymost_setVisibility(0.f);
                    polymost_domost(x0,fy0,x1,fy1);
                    flatskyrender = 0;
                }
                else
                {
                    if (r_yshearing)
                        ghoriz = (qglobalhoriz*(1.f/65536.f)-float(ydimen>>1))*(dapyscale-65536.f)*(1.f/65536.f)+float(ydimen>>1);

                    float const dd = fxdimen*.0000001f; //Adjust sky depth based on screen size!
                    float vv[2];
                    float t = (float)((1<<(picsiz[globalpicnum]&15))<<dapskybits);
                    vv[1] = dd*((float)xdimscale*fviewingrange) * (1.f/(daptileyscale*65536.f));
                    vv[0] = dd*((float)((tilesiz[globalpicnum].y>>1)+dapyoffs)) - vv[1]*ghoriz;
                    int i = (1<<(picsiz[globalpicnum]>>4)); if (i != tilesiz[globalpicnum].y) i += i;
                    vec3f_t o;

                    if ((tilesiz[globalpicnum].y * daptileyscale * (1.f/65536.f)) > 256)
                    {
                        //Hack to draw black rectangle below sky when looking down...
                        xtex.d = xtex.u = xtex.v = 0;

                        ytex.d = gxyaspect * (1.0 / 262144.0);
                        ytex.u = 0;
                        ytex.v = double(tilesiz[globalpicnum].y - 1) * ytex.d;

                        otex.d = -ghoriz * ytex.d;
                        otex.u = 0;
                        otex.v = double(tilesiz[globalpicnum].y - 1) * otex.d;

                        o.y = ((float)tilesiz[globalpicnum].y*dd-vv[0])/vv[1];

                        if ((o.y > fy0) && (o.y > fy1))
                            polymost_domost(x0,o.y,x1,o.y);
                        else if ((o.y > fy0) != (o.y > fy1))
                        {
                            //  fy0                      fy1
                            //     \                    /
                            //oy----------      oy----------
                            //        \              /
                            //         fy1        fy0
                            o.x = (o.y-fy0)*(x1-x0)/(fy1-fy0) + x0;
                            if (o.y > fy0)
                            {
                                polymost_domost(x0,o.y,o.x,o.y);
                                polymost_domost(o.x,o.y,x1,fy1);
                            }
                            else
                            {
                                polymost_domost(x0,fy0,o.x,o.y);
                                polymost_domost(o.x,o.y,x1,o.y);
                            }
                        }
                        else
                            polymost_domost(x0,fy0,x1,fy1);

#if 0
                        //Hack to draw color rectangle above sky when looking up...
                        xtex.d = xtex.u = xtex.v = 0;

                        ytex.d = gxyaspect * (1.f / -262144.f);
                        ytex.u = 0;
                        ytex.v = 0;

                        otex.d = -ghoriz * ytex.d;
                        otex.u = 0;
                        otex.v = 0;

                        o.y = -vv[0]/vv[1];

                        if ((o.y < fy0) && (o.y < fy1))
                            polymost_domost(x1,o.y,x0,o.y);
                        else if ((o.y < fy0) != (o.y < fy1))
                        {
                            o.x = (o.y-fy0)*(x1-x0)/(fy1-fy0) + x0;
                            if (o.y < fy0)
                            {
                                polymost_domost(o.x,o.y,x0,o.y);
                                polymost_domost(x1,fy1,o.x,o.y);
                            }
                            else
                            {
                                polymost_domost(o.x,o.y,x0,fy0);
                                polymost_domost(x1,o.y,o.x,o.y);
                            }
                        }
                        else
                            polymost_domost(x1,fy1,x0,fy0);
#endif
                    }
                    else
                        skyclamphack = 0;

                    xtex.d = xtex.v = 0;
                    ytex.d = ytex.u = 0;
                    otex.d = dd;
                    xtex.u = otex.d * (t * double(((uint64_t)xdimscale * yxaspect) * viewingrange)) *
                                      (1.0 / (16384.0 * 65536.0 * 65536.0 * 5.0 * 1024.0));
                    ytex.v = vv[1];
                    otex.v = r_parallaxskypanning ? vv[0] + dd*(float)sec->floorypanning*(float)i*(1.f/256.f) : vv[0];

                    int const npot = (1<<(picsiz[globalpicnum]&15)) != tilesiz[globalpicnum].x;
                    int const xpanning = (r_parallaxskypanning?sec->floorxpanning:0);

                    i = globalpicnum;
                    float const r = (fy1-fy0)/(x1-x0); //slope of line
                    o.y = fviewingrange/(ghalfx*256.f); o.z = 1.f/o.y;

                    int y = ((int32_t)(((x0-ghalfx)*o.y)+fglobalang)>>(11-dapskybits));
                    float fx = x0;
                    do
                    {
                        globalpicnum = dapskyoff[y&((1<<dapskybits)-1)]+i;
                        if (npot)
                        {
                            float fx = ((float)((y<<(11-dapskybits))-fglobalang))*o.z+ghalfx;
                            int tang = (y<<(11-dapskybits))&2047;
                            otex.u = otex.d*(t*((float)(tang)) * (1.f/2048.f) + xpanning) - xtex.u*fx;
                        }
                        else
                            otex.u = otex.d*(t*((float)(fglobalang-(y<<(11-dapskybits)))) * (1.f/2048.f) + xpanning) - xtex.u*ghalfx;
                        y++;
                        o.x = fx; fx = ((float)((y<<(11-dapskybits))-fglobalang))*o.z+ghalfx;
                        if (fx > x1) { fx = x1; i = -1; }

                        pow2xsplit = 0; polymost_domost(o.x,(o.x-x0)*r+fy0,fx,(fx-x0)*r+fy0); //flor
                    }
                    while (i >= 0);
                }
                ghoriz = ghorizbak;
            }
            else  //NOTE: code copied from ceiling code... lots of duplicated stuff :/
            {
                //Skybox code for parallax floor!
                float sky_t0, sky_t1; // _nx0, _ny0, _nx1, _ny1;
                float sky_ryp0, sky_ryp1, sky_x0, sky_x1, sky_cy0, sky_fy0, sky_cy1, sky_fy1, sky_ox0, sky_ox1;
                static vec2f_t const skywal[4] = { { -512, -512 }, { 512, -512 }, { 512, 512 }, { -512, 512 } };

                pow2xsplit = 0;
                skyclamphack = 1;

                for (bssize_t i=0; i<4; i++)
                {
                    walpos = skywal[i&3];
                    vec2f_t skyp0 = { walpos.y * gcosang - walpos.x * gsinang,
                                      walpos.x * gcosang2 + walpos.y * gsinang2 };

                    walpos = skywal[(i + 1) & 3];
                    vec2f_t skyp1 = { walpos.y * gcosang - walpos.x * gsinang,
                                      walpos.x * gcosang2 + walpos.y * gsinang2 };

                    vec2f_t const oskyp0 = skyp0;

                    //Clip to close parallel-screen plane
                    if (skyp0.y < SCISDIST)
                    {
                        if (skyp1.y < SCISDIST) continue;
                        sky_t0 = (SCISDIST - skyp0.y) / (skyp1.y - skyp0.y);
                        skyp0  = { (skyp1.x - skyp0.x) * sky_t0 + skyp0.x, SCISDIST };
                    }
                    else { sky_t0 = 0.f; }

                    if (skyp1.y < SCISDIST)
                    {
                        sky_t1  = (SCISDIST - oskyp0.y) / (skyp1.y - oskyp0.y);
                        skyp1 = { (skyp1.x - oskyp0.x) * sky_t1 + oskyp0.x, SCISDIST };
                    }
                    else { sky_t1 = 1.f; }

                    sky_ryp0 = 1.f/skyp0.y; sky_ryp1 = 1.f/skyp1.y;

                    //Generate screen coordinates for front side of wall
                    sky_x0 = ghalfx*skyp0.x*sky_ryp0 + ghalfx;
                    sky_x1 = ghalfx*skyp1.x*sky_ryp1 + ghalfx;
                    if ((sky_x1 <= sky_x0) || (sky_x0 >= x1) || (x0 >= sky_x1)) continue;

                    sky_ryp0 *= gyxscale; sky_ryp1 *= gyxscale;

                    sky_cy0 = -8192.f*sky_ryp0 + ghoriz;
                    sky_fy0 =  8192.f*sky_ryp0 + ghoriz;
                    sky_cy1 = -8192.f*sky_ryp1 + ghoriz;
                    sky_fy1 =  8192.f*sky_ryp1 + ghoriz;

                    sky_ox0 = sky_x0; sky_ox1 = sky_x1;

                    //Make sure: x0<=_x0<_x1<=x1
                    float nfy[2] = { fy0, fy1 };

                    if (sky_x0 < x0)
                    {
                        float const t = (x0-sky_x0)/(sky_x1-sky_x0);
                        sky_cy0 += (sky_cy1-sky_cy0)*t;
                        sky_fy0 += (sky_fy1-sky_fy0)*t;
                        sky_x0 = x0;
                    }
                    else if (sky_x0 > x0) nfy[0] += (sky_x0-x0)*(fy1-fy0)/(x1-x0);

                    if (sky_x1 > x1)
                    {
                        float const t = (x1-sky_x1)/(sky_x1-sky_x0);
                        sky_cy1 += (sky_cy1-sky_cy0)*t;
                        sky_fy1 += (sky_fy1-sky_fy0)*t;
                        sky_x1 = x1;
                    }
                    else if (sky_x1 < x1) nfy[1] += (sky_x1-x1)*(fy1-fy0)/(x1-x0);

                    //   (skybox floor)
                    //(_x0,_fy0)-(_x1,_fy1)
                    //   (skybox wall)
                    //(_x0,_cy0)-(_x1,_cy1)
                    //   (skybox ceiling)
                    //(_x0,nfy0)-(_x1,nfy1)

                    //floor of skybox
                    drawingskybox = 6; //floor/6th texture/index 5 of skybox
                    float const ft[4] = { 512 / 16, 512 / -16, fcosglobalang * (1.f / 2147483648.f),
                                          fsinglobalang * (1.f / 2147483648.f) };

                    xtex.d = 0;
                    ytex.d = gxyaspect*(1.0/4194304.0);
                    otex.d = -ghoriz*ytex.d;
                    xtex.u = ft[3]*fviewingrange*(-1.0/65536.0);
                    xtex.v = ft[2]*fviewingrange*(-1.0/65536.0);
                    ytex.u = ft[0]*ytex.d; ytex.v = ft[1]*ytex.d;
                    otex.u = ft[0]*otex.d; otex.v = ft[1]*otex.d;
                    otex.u += (ft[2]-xtex.u)*ghalfx;
                    otex.v -= (ft[3]+xtex.v)*ghalfx;
                    xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; //y-flip skybox floor

                    if ((sky_fy0 > nfy[0]) && (sky_fy1 > nfy[1]))
                        polymost_domost(sky_x0,sky_fy0,sky_x1,sky_fy1);
                    else if ((sky_fy0 > nfy[0]) != (sky_fy1 > nfy[1]))
                    {
                        //(ox,oy) is intersection of: (_x0,_fy0)-(_x1,_fy1)
                        //                            (_x0,nfy0)-(_x1,nfy1)
                        float const t = (sky_fy0-nfy[0])/(nfy[1]-nfy[0]-sky_fy1+sky_fy0);
                        vec2f_t const o = { sky_x0 + (sky_x1-sky_x0)*t, sky_fy0 + (sky_fy1-sky_fy0)*t };
                        if (nfy[0] > sky_fy0)
                        {
                            polymost_domost(sky_x0,nfy[0],o.x,o.y);
                            polymost_domost(o.x,o.y,sky_x1,sky_fy1);
                        }
                        else
                        {
                            polymost_domost(sky_x0,sky_fy0,o.x,o.y);
                            polymost_domost(o.x,o.y,sky_x1,nfy[1]);
                        }
                    }
                    else
                        polymost_domost(sky_x0,nfy[0],sky_x1,nfy[1]);

                    //wall of skybox
                    drawingskybox = i+1; //i+1th texture/index i of skybox
                    xtex.d = (sky_ryp0-sky_ryp1)*gxyaspect*(1.0/512.0) / (sky_ox0-sky_ox1);
                    ytex.d = 0;
                    otex.d = sky_ryp0*gxyaspect*(1.0/512.0) - xtex.d*sky_ox0;
                    xtex.u = (sky_t0*sky_ryp0 - sky_t1*sky_ryp1)*gxyaspect*(64.0/512.0) / (sky_ox0-sky_ox1);
                    otex.u = sky_t0*sky_ryp0*gxyaspect*(64.0/512.0) - xtex.u*sky_ox0;
                    ytex.u = 0;
                    sky_t0 = -8192.f*sky_ryp0 + ghoriz;
                    sky_t1 = -8192.f*sky_ryp1 + ghoriz;
                    float const t = ((xtex.d*sky_ox0 + otex.d)*8.f) / ((sky_ox1-sky_ox0) * sky_ryp0 * 2048.f);
                    xtex.v = (sky_t0-sky_t1)*t;
                    ytex.v = (sky_ox1-sky_ox0)*t;
                    otex.v = -xtex.v*sky_ox0 - ytex.v*sky_t0;

                    if ((sky_cy0 > nfy[0]) && (sky_cy1 > nfy[1]))
                        polymost_domost(sky_x0,sky_cy0,sky_x1,sky_cy1);
                    else if ((sky_cy0 > nfy[0]) != (sky_cy1 > nfy[1]))
                    {
                        //(ox,oy) is intersection of: (_x0,_fy0)-(_x1,_fy1)
                        //                            (_x0,nfy0)-(_x1,nfy1)
                        float const t = (sky_cy0-nfy[0])/(nfy[1]-nfy[0]-sky_cy1+sky_cy0);
                        vec2f_t const o = { sky_x0 + (sky_x1 - sky_x0) * t, sky_cy0 + (sky_cy1 - sky_cy0) * t };
                        if (nfy[0] > sky_cy0)
                        {
                            polymost_domost(sky_x0,nfy[0],o.x,o.y);
                            polymost_domost(o.x,o.y,sky_x1,sky_cy1);
                        }
                        else
                        {
                            polymost_domost(sky_x0,sky_cy0,o.x,o.y);
                            polymost_domost(o.x,o.y,sky_x1,nfy[1]);
                        }
                    }
                    else
                        polymost_domost(sky_x0,nfy[0],sky_x1,nfy[1]);
                }

                //Ceiling of skybox
                drawingskybox = 5; //ceiling/5th texture/index 4 of skybox
                float const ft[4] = { 512 / 16, -512 / -16, fcosglobalang * (1.f / 2147483648.f),
                                      fsinglobalang * (1.f / 2147483648.f) };

                xtex.d = 0;
                ytex.d = gxyaspect*(-1.0/4194304.0);
                otex.d = -ghoriz*ytex.d;
                xtex.u = ft[3]*fviewingrange*(-1.0/65536.0);
                xtex.v = ft[2]*fviewingrange*(-1.0/65536.0);
                ytex.u = ft[0]*ytex.d; ytex.v = ft[1]*ytex.d;
                otex.u = ft[0]*otex.d; otex.v = ft[1]*otex.d;
                otex.u += (ft[2]-xtex.u)*ghalfx;
                otex.v -= (ft[3]+xtex.v)*ghalfx;

                polymost_domost(x0,fy0,x1,fy1);

                skyclamphack = 0;
                drawingskybox = 0;
            }

            skyclamphack = 0;
            skyzbufferhack = 0;
            if (!nofog)
                polymost_setFogEnabled(true);
        }
        
        doeditorcheck = 0;

        // Ceiling
 
        if (searchit == 2      
#ifdef YAX_ENABLE
            && (yax_getbunch(sectnum, YAX_CEILING) < 0 || showinvisibility || (sec->floorstat&(256+128)) || klabs(yax_globallev-YAX_MAXDRAWS)==YAX_MAXDRAWS)
#endif
            )
        {
            psectnum = sectnum;
            pwallnum = wallnum;
            psearchstat = 1;
            doeditorcheck = 1;
        }

#ifdef YAX_ENABLE
        yax_holencf[YAX_CEILING] = 0;
        yax_drawcf = YAX_CEILING;
#endif

        globalpicnum = sec->ceilingpicnum;
        globalshade = sec->ceilingshade;
        globalpal = sec->ceilingpal;
        globalorientation = sec->ceilingstat;
        globvis = (sector[sectnum].visibility != 0) ?
                  mulscale4(globalcisibility, (uint8_t)(sector[sectnum].visibility + 16)) :
                  globalcisibility;
        globvis2 = (sector[sectnum].visibility != 0) ?
                  mulscale4(globalcisibility2, (uint8_t)(sector[sectnum].visibility + 16)) :
                  globalcisibility2;
        polymost_setVisibility(globvis2);

        tileUpdatePicnum(&globalpicnum, sectnum);


        dapskyoff = getpsky(globalpicnum, &dapyscale, &dapskybits, &dapyoffs, &daptileyscale);

        global_cf_fogpal = sec->fogpal;
        global_cf_shade = sec->ceilingshade, global_cf_pal = sec->ceilingpal; global_cf_z = sec->ceilingz;  // REFACT
        global_cf_xpanning = sec->ceilingxpanning; global_cf_ypanning = sec->ceilingypanning, global_cf_heinum = sec->ceilingheinum;
        global_getzofslope_func = &fgetceilzofslope;

        if (!(globalorientation&1))
        {
            int32_t cz = getceilzofslope(sectnum, globalposx, globalposy);
            if (globalposz >= cz)
                polymost_internal_nonparallaxed(n0, n1, ryp0, ryp1, x0, x1, cy0, cy1, sectnum);
        }
        else if ((nextsectnum < 0) || (!(sector[nextsectnum].ceilingstat&1)))
        {
            //Parallaxing sky... hacked for Ken's mountain texture
            if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
                calc_and_apply_fog_factor(sec->ceilingshade, sec->visibility, sec->ceilingpal, 0.005f);

            globvis2 = globalpisibility;
            if (sec->visibility != 0)
                globvis2 = mulscale4(globvis2, (uint8_t)(sec->visibility + 16));
            float viscale = xdimscale*fxdimen*(.0000001f/256.f);
            polymost_setVisibility(globvis2*viscale);

            //Use clamping for tiled sky textures
            //(don't wrap around edges if the sky use multiple panels)
            for (bssize_t i=(1<<dapskybits)-1; i>0; i--)
                if (dapskyoff[i] != dapskyoff[i-1])
                    { skyclamphack = r_parallaxskyclamping; break; }

            skyzbufferhack = 1;

            if (!usehightile || !hicfindskybox(globalpicnum, globalpal))
            {
                float const ghorizbak = ghoriz;
                if (r_flatsky && ! r_yshearing)
                {
                    pow2xsplit = 0;
                    skyclamphack = 0;
                    flatskyrender = 1;
                    globalshade += globvis2*xdimscale*fviewingrange*(1.f / (64.f * 65536.f * 256.f * 1024.f));
                    polymost_setVisibility(0.f);
                    polymost_domost(x1,cy1,x0,cy0);
                    flatskyrender = 0;
                }
                else
                {
                    if (r_yshearing)
                        ghoriz = (qglobalhoriz*(1.f/65536.f)-float(ydimen>>1))*(dapyscale-65536.f)*(1.f/65536.f)+float(ydimen>>1);

                    float const dd = fxdimen*.0000001f; //Adjust sky depth based on screen size!
                    float vv[2];
                    float t = (float)((1<<(picsiz[globalpicnum]&15))<<dapskybits);
                    vv[1] = dd*((float)xdimscale*fviewingrange) * (1.f/(daptileyscale*65536.f));
                    vv[0] = dd*((float)((tilesiz[globalpicnum].y>>1)+dapyoffs)) - vv[1]*ghoriz;
                    int i = (1<<(picsiz[globalpicnum]>>4)); if (i != tilesiz[globalpicnum].y) i += i;
                    vec3f_t o;

                    if ((tilesiz[globalpicnum].y * daptileyscale * (1.f/65536.f)) > 256 && !r_yshearing)
                    {
#if 0
                        //Hack to draw black rectangle below sky when looking down...
                        xtex.d = xtex.u = xtex.v = 0;

                        ytex.d = gxyaspect * (1.f / 262144.f);
                        ytex.u = 0;
                        ytex.v = (float)(tilesiz[globalpicnum].y - 1) * ytex.d;

                        otex.d = -ghoriz * ytex.d;
                        otex.u = 0;
                        otex.v = (float)(tilesiz[globalpicnum].y - 1) * otex.d;

                        o.y = ((float)tilesiz[globalpicnum].y*dd-vv[0])/vv[1];

                        if ((o.y > cy0) && (o.y > cy1))
                            polymost_domost(x0,o.y,x1,o.y);
                        else if ((o.y > cy0) != (o.y > cy1))
                        {
                            o.x = (o.y-cy0)*(x1-x0)/(cy1-cy0) + x0;
                            if (o.y > cy0)
                            {
                                polymost_domost(x0,o.y,o.x,o.y);
                                polymost_domost(o.x,o.y,x1,cy1);
                            }
                            else
                            {
                                polymost_domost(x0,cy0,o.x,o.y);
                                polymost_domost(o.x,o.y,x1,o.y);
                            }
                        }
                        else
                            polymost_domost(x0,cy0,x1,cy1);
#endif

                        //Hack to draw color rectangle above sky when looking up...
                        xtex.d = xtex.u = xtex.v = 0;

                        ytex.d = gxyaspect * (1.0 / -262144.0);
                        ytex.u = 0;
                        ytex.v = 0;

                        otex.d = -ghoriz * ytex.d;
                        otex.u = 0;
                        otex.v = 0;

                        o.y = -vv[0]/vv[1];

                        if ((o.y < cy0) && (o.y < cy1))
                            polymost_domost(x1,o.y,x0,o.y);
                        else if ((o.y < cy0) != (o.y < cy1))
                        {
                            /*         cy1        cy0
                            //        /              \
                            //oy----------      oy---------
                            //    /                   \
                            //  cy0                     cy1 */
                            o.x = (o.y-cy0)*(x1-x0)/(cy1-cy0) + x0;
                            if (o.y < cy0)
                            {
                                polymost_domost(o.x,o.y,x0,o.y);
                                polymost_domost(x1,cy1,o.x,o.y);
                            }
                            else
                            {
                                polymost_domost(o.x,o.y,x0,cy0);
                                polymost_domost(x1,o.y,o.x,o.y);
                            }
                        }
                        else
                            polymost_domost(x1,cy1,x0,cy0);
                    }
                    else
                        skyclamphack = 0;

                    xtex.d = xtex.v = 0;
                    ytex.d = ytex.u = 0;
                    otex.d = dd;
                    xtex.u = otex.d * (t * double(((uint64_t)xdimscale * yxaspect) * viewingrange)) *
                                      (1.0 / (16384.0 * 65536.0 * 65536.0 * 5.0 * 1024.0));
                    ytex.v = vv[1];
                    otex.v = r_parallaxskypanning ? vv[0] + dd*(float)sec->ceilingypanning*(float)i*(1.f/256.f) : vv[0];

                    int const npot = (1<<(picsiz[globalpicnum]&15)) != tilesiz[globalpicnum].x;
                    int const xpanning = (r_parallaxskypanning?sec->ceilingxpanning:0);

                    i = globalpicnum;
                    float const r = (cy1-cy0)/(x1-x0); //slope of line
                    o.y = fviewingrange/(ghalfx*256.f); o.z = 1.f/o.y;

                    int y = ((int32_t)(((x0-ghalfx)*o.y)+fglobalang)>>(11-dapskybits));
                    float fx = x0;
                    do
                    {
                        globalpicnum = dapskyoff[y&((1<<dapskybits)-1)]+i;
                        if (npot)
                        {
                            float fx = ((float)((y<<(11-dapskybits))-fglobalang))*o.z+ghalfx;
                            int tang = (y<<(11-dapskybits))&2047;
                            otex.u = otex.d*(t*((float)(tang)) * (1.f/2048.f) + xpanning) - xtex.u*fx;
                        }
                        else
                            otex.u = otex.d*(t*((float)(fglobalang-(y<<(11-dapskybits)))) * (1.f/2048.f) + xpanning) - xtex.u*ghalfx;
                        y++;
                        o.x = fx; fx = (((float) (y<<(11-dapskybits))-fglobalang))*o.z+ghalfx;
                        if (fx > x1) { fx = x1; i = -1; }

                        pow2xsplit = 0; polymost_domost(fx,(fx-x0)*r+cy0,o.x,(o.x-x0)*r+cy0); //ceil
                    }
                    while (i >= 0);

                }
                ghoriz = ghorizbak;
            }
            else
            {
                //Skybox code for parallax ceiling!
                float sky_t0, sky_t1; // _nx0, _ny0, _nx1, _ny1;
                float sky_ryp0, sky_ryp1, sky_x0, sky_x1, sky_cy0, sky_fy0, sky_cy1, sky_fy1, sky_ox0, sky_ox1;
                static vec2f_t const skywal[4] = { { -512, -512 }, { 512, -512 }, { 512, 512 }, { -512, 512 } };

                pow2xsplit = 0;
                skyclamphack = 1;

                for (bssize_t i=0; i<4; i++)
                {
                    walpos = skywal[i&3];
                    vec2f_t skyp0 = { walpos.y * gcosang - walpos.x * gsinang,
                                      walpos.x * gcosang2 + walpos.y * gsinang2 };

                    walpos = skywal[(i + 1) & 3];
                    vec2f_t skyp1 = { walpos.y * gcosang - walpos.x * gsinang,
                                      walpos.x * gcosang2 + walpos.y * gsinang2 };

                    vec2f_t const oskyp0 = skyp0;

                    //Clip to close parallel-screen plane
                    if (skyp0.y < SCISDIST)
                    {
                        if (skyp1.y < SCISDIST) continue;
                        sky_t0 = (SCISDIST - skyp0.y) / (skyp1.y - skyp0.y);
                        skyp0  = { (skyp1.x - skyp0.x) * sky_t0 + skyp0.x, SCISDIST };
                    }
                    else { sky_t0 = 0.f; }

                    if (skyp1.y < SCISDIST)
                    {
                        sky_t1 = (SCISDIST - oskyp0.y) / (skyp1.y - oskyp0.y);
                        skyp1  = { (skyp1.x - oskyp0.x) * sky_t1 + oskyp0.x, SCISDIST };
                    }
                    else { sky_t1 = 1.f; }

                    sky_ryp0 = 1.f/skyp0.y; sky_ryp1 = 1.f/skyp1.y;

                    //Generate screen coordinates for front side of wall
                    sky_x0 = ghalfx*skyp0.x*sky_ryp0 + ghalfx;
                    sky_x1 = ghalfx*skyp1.x*sky_ryp1 + ghalfx;
                    if ((sky_x1 <= sky_x0) || (sky_x0 >= x1) || (x0 >= sky_x1)) continue;

                    sky_ryp0 *= gyxscale; sky_ryp1 *= gyxscale;

                    sky_cy0 = -8192.f*sky_ryp0 + ghoriz;
                    sky_fy0 =  8192.f*sky_ryp0 + ghoriz;
                    sky_cy1 = -8192.f*sky_ryp1 + ghoriz;
                    sky_fy1 =  8192.f*sky_ryp1 + ghoriz;

                    sky_ox0 = sky_x0; sky_ox1 = sky_x1;

                    //Make sure: x0<=_x0<_x1<=x1
                    float ncy[2] = { cy0, cy1 };

                    if (sky_x0 < x0)
                    {
                        float const t = (x0-sky_x0)/(sky_x1-sky_x0);
                        sky_cy0 += (sky_cy1-sky_cy0)*t;
                        sky_fy0 += (sky_fy1-sky_fy0)*t;
                        sky_x0 = x0;
                    }
                    else if (sky_x0 > x0) ncy[0] += (sky_x0-x0)*(cy1-cy0)/(x1-x0);

                    if (sky_x1 > x1)
                    {
                        float const t = (x1-sky_x1)/(sky_x1-sky_x0);
                        sky_cy1 += (sky_cy1-sky_cy0)*t;
                        sky_fy1 += (sky_fy1-sky_fy0)*t;
                        sky_x1 = x1;
                    }
                    else if (sky_x1 < x1) ncy[1] += (sky_x1-x1)*(cy1-cy0)/(x1-x0);

                    //   (skybox ceiling)
                    //(_x0,_cy0)-(_x1,_cy1)
                    //   (skybox wall)
                    //(_x0,_fy0)-(_x1,_fy1)
                    //   (skybox floor)
                    //(_x0,ncy0)-(_x1,ncy1)

                    //ceiling of skybox
                    drawingskybox = 5; //ceiling/5th texture/index 4 of skybox
                    float const ft[4] = { 512 / 16, -512 / -16, fcosglobalang * (1.f / 2147483648.f),
                                          fsinglobalang * (1.f / 2147483648.f) };

                    xtex.d = 0;
                    ytex.d = gxyaspect*(-1.0/4194304.0);
                    otex.d = -ghoriz*ytex.d;
                    xtex.u = ft[3]*fviewingrange*(-1.0/65536.0);
                    xtex.v = ft[2]*fviewingrange*(-1.0/65536.0);
                    ytex.u = ft[0]*ytex.d; ytex.v = ft[1]*ytex.d;
                    otex.u = ft[0]*otex.d; otex.v = ft[1]*otex.d;
                    otex.u += (ft[2]-xtex.u)*ghalfx;
                    otex.v -= (ft[3]+xtex.v)*ghalfx;


                    if ((sky_cy0 < ncy[0]) && (sky_cy1 < ncy[1]))
                        polymost_domost(sky_x1,sky_cy1,sky_x0,sky_cy0);
                    else if ((sky_cy0 < ncy[0]) != (sky_cy1 < ncy[1]))
                    {
                        //(ox,oy) is intersection of: (_x0,_cy0)-(_x1,_cy1)
                        //                            (_x0,ncy0)-(_x1,ncy1)
                        float const t = (sky_cy0-ncy[0])/(ncy[1]-ncy[0]-sky_cy1+sky_cy0);
                        vec2f_t const o = { sky_x0 + (sky_x1-sky_x0)*t, sky_cy0 + (sky_cy1-sky_cy0)*t };
                        if (ncy[0] < sky_cy0)
                        {
                            polymost_domost(o.x,o.y,sky_x0,ncy[0]);
                            polymost_domost(sky_x1,sky_cy1,o.x,o.y);
                        }
                        else
                        {
                            polymost_domost(o.x,o.y,sky_x0,sky_cy0);
                            polymost_domost(sky_x1,ncy[1],o.x,o.y);
                        }
                    }
                    else
                        polymost_domost(sky_x1,ncy[1],sky_x0,ncy[0]);

                    //wall of skybox
                    drawingskybox = i+1; //i+1th texture/index i of skybox
                    xtex.d = (sky_ryp0-sky_ryp1)*gxyaspect*(1.0/512.0) / (sky_ox0-sky_ox1);
                    ytex.d = 0;
                    otex.d = sky_ryp0*gxyaspect*(1.0/512.0) - xtex.d*sky_ox0;
                    xtex.u = (sky_t0*sky_ryp0 - sky_t1*sky_ryp1)*gxyaspect*(64.0/512.0) / (sky_ox0-sky_ox1);
                    otex.u = sky_t0*sky_ryp0*gxyaspect*(64.0/512.0) - xtex.u*sky_ox0;
                    ytex.u = 0;
                    sky_t0 = -8192.f*sky_ryp0 + ghoriz;
                    sky_t1 = -8192.f*sky_ryp1 + ghoriz;
                    float const t = ((xtex.d*sky_ox0 + otex.d)*8.f) / ((sky_ox1-sky_ox0) * sky_ryp0 * 2048.f);
                    xtex.v = (sky_t0-sky_t1)*t;
                    ytex.v = (sky_ox1-sky_ox0)*t;
                    otex.v = -xtex.v*sky_ox0 - ytex.v*sky_t0;

                    if ((sky_fy0 < ncy[0]) && (sky_fy1 < ncy[1]))
                        polymost_domost(sky_x1,sky_fy1,sky_x0,sky_fy0);
                    else if ((sky_fy0 < ncy[0]) != (sky_fy1 < ncy[1]))
                    {
                        //(ox,oy) is intersection of: (_x0,_fy0)-(_x1,_fy1)
                        //                            (_x0,ncy0)-(_x1,ncy1)
                        float const t = (sky_fy0-ncy[0])/(ncy[1]-ncy[0]-sky_fy1+sky_fy0);
                        vec2f_t const o = { sky_x0 + (sky_x1 - sky_x0) * t, sky_fy0 + (sky_fy1 - sky_fy0) * t };
                        if (ncy[0] < sky_fy0)
                        {
                            polymost_domost(o.x,o.y,sky_x0,ncy[0]);
                            polymost_domost(sky_x1,sky_fy1,o.x,o.y);
                        }
                        else
                        {
                            polymost_domost(o.x,o.y,sky_x0,sky_fy0);
                            polymost_domost(sky_x1,ncy[1],o.x,o.y);
                        }
                    }
                    else
                        polymost_domost(sky_x1,ncy[1],sky_x0,ncy[0]);
                }

                //Floor of skybox
                drawingskybox = 6; //floor/6th texture/index 5 of skybox
                float const ft[4] = { 512 / 16, 512 / -16, fcosglobalang * (1.f / 2147483648.f),
                                      fsinglobalang * (1.f / 2147483648.f) };

                xtex.d = 0;
                ytex.d = gxyaspect*(1.0/4194304.0);
                otex.d = -ghoriz*ytex.d;
                xtex.u = ft[3]*fviewingrange*(-1.0/65536.0);
                xtex.v = ft[2]*fviewingrange*(-1.0/65536.0);
                ytex.u = ft[0]*ytex.d; ytex.v = ft[1]*ytex.d;
                otex.u = ft[0]*otex.d; otex.v = ft[1]*otex.d;
                otex.u += (ft[2]-xtex.u)*ghalfx;
                otex.v -= (ft[3]+xtex.v)*ghalfx;
                xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; //y-flip skybox floor
                polymost_domost(x1,cy1,x0,cy0);

                skyclamphack = 0;
                drawingskybox = 0;
            }

            skyclamphack = 0;
            skyzbufferhack = 0;
            if (!nofog)
                polymost_setFogEnabled(true);
        }
        
        doeditorcheck = 0;

#ifdef YAX_ENABLE
        if (g_nodraw)
        {
            int32_t baselevp, checkcf, i, j;
            int16_t bn[2];
# if 0
            int32_t obunchchk = (1 && yax_globalbunch>=0 &&
                                 haveymost[yax_globalbunch>>3]&pow2char[yax_globalbunch&7]);

            // if (obunchchk)
            const int32_t x2 = yax_globalbunch*xdimen;
# endif
            baselevp = (yax_globallev == YAX_MAXDRAWS);

            yax_getbunches(sectnum, &bn[0], &bn[1]);
            checkcf = (bn[0]>=0) + ((bn[1]>=0)<<1);
            if (!baselevp)
                checkcf &= (1<<yax_globalcf);

            for (i=0; i<2; i++)
                if (checkcf&(1<<i))
                {
                    if ((haveymost[bn[i]>>3]&pow2char[bn[i]&7])==0)
                    {
                        // init yax *most arrays for that bunch
                        haveymost[bn[i]>>3] |= pow2char[bn[i]&7];
                        yax_vsp[bn[i]*2][1].x = xbl;
                        yax_vsp[bn[i]*2][2].x = xbr;
                        yax_vsp[bn[i]*2][1].cy[0] = xbb;
                        yax_vsp[bn[i]*2][2].cy[0] = xbb;
                        yax_vsp_finalize_init(bn[i]*2, 3);
                        yax_vsp[bn[i]*2+1][1].x = xbl;
                        yax_vsp[bn[i]*2+1][2].x = xbr;
                        yax_vsp[bn[i]*2+1][1].cy[0] = xbt;
                        yax_vsp[bn[i]*2+1][2].cy[0] = xbt;
                        yax_vsp_finalize_init(bn[i]*2+1, 3);
                    }

                    for (j = 0; j < yax_holencf[i]; j++)
                    {
                        yax_hole_t *hole = &yax_holecf[i][j];
                        yax_polymost_domost(bn[i]*2, hole->x0, hole->cy[0], hole->x1, hole->cy[1]);
                        yax_polymost_domost(bn[i]*2+1, hole->x1, hole->fy[1], hole->x0, hole->fy[0]);
                    }
                }
        }
#endif

        // Wall

#ifdef YAX_ENABLE
        yax_drawcf = -1;
#endif

        xtex.d = (ryp0-ryp1)*gxyaspect / (x0-x1);
        ytex.d = 0;
        otex.d = ryp0*gxyaspect - xtex.d*x0;

        xtex.u = (t0*ryp0 - t1*ryp1)*gxyaspect*(float)wal->xrepeat*8.f / (x0-x1);
        otex.u = t0*ryp0*gxyaspect*wal->xrepeat*8.0 - xtex.u*x0;
        otex.u += (float)wal->xpanning*otex.d;
        xtex.u += (float)wal->xpanning*xtex.d;
        ytex.u = 0;

        float const ogux = xtex.u, oguy = ytex.u, oguo = otex.u;

        Bassert(domostpolymethod == DAMETH_NOMASK);
        domostpolymethod = DAMETH_WALL;

#ifdef YAX_ENABLE
        if (yax_nomaskpass==0 || !yax_isislandwall(wallnum, !yax_globalcf) || (yax_nomaskdidit=1, 0))
#endif
        if (nextsectnum >= 0)
        {
            fgetzsofslope((usectorptr_t)&sector[nextsectnum],n0.x,n0.y,&cz,&fz);
            float const ocy0 = (cz-globalposz)*ryp0 + ghoriz;
            float const ofy0 = (fz-globalposz)*ryp0 + ghoriz;
            fgetzsofslope((usectorptr_t)&sector[nextsectnum],n1.x,n1.y,&cz,&fz);
            float const ocy1 = (cz-globalposz)*ryp1 + ghoriz;
            float const ofy1 = (fz-globalposz)*ryp1 + ghoriz;

            if ((wal->cstat&48) == 16) maskwall[maskwallcnt++] = z;

            if (((cy0 < ocy0) || (cy1 < ocy1)) && (!((sec->ceilingstat&sector[nextsectnum].ceilingstat)&1)))
            {
                if (searchit == 2)
                {
                    psectnum = sectnum;
                    pbottomwall = pwallnum = wallnum;
                    pisbottomwall = 0;
                    psearchstat = 0;
                    doeditorcheck = 1;
                }
                globalpicnum = wal->picnum; globalshade = wal->shade; globalpal = (int32_t)((uint8_t)wal->pal);
                globvis = globalvisibility;
                if (sector[sectnum].visibility != 0) globvis = mulscale4(globvis, (uint8_t)(sector[sectnum].visibility+16));
                globvis2 = globalvisibility2;
                if (sector[sectnum].visibility != 0) globvis2 = mulscale4(globvis2, (uint8_t)(sector[sectnum].visibility+16));
                polymost_setVisibility(globvis2);
                globalorientation = wal->cstat;
                tileUpdatePicnum(&globalpicnum, wallnum+16384);

                int i = (!(wal->cstat&4)) ? sector[nextsectnum].ceilingz : sec->ceilingz;

                // over
                calc_ypanning(i, ryp0, ryp1, x0, x1, wal->ypanning, wal->yrepeat, wal->cstat&4);

                if (wal->cstat&8) //xflip
                {
                    float const t = (float)(wal->xrepeat*8 + wal->xpanning*2);
                    xtex.u = xtex.d*t - xtex.u;
                    ytex.u = ytex.d*t - ytex.u;
                    otex.u = otex.d*t - otex.u;
                }
                if (wal->cstat&256) { xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; } //yflip

                if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
                    calc_and_apply_fog(fogshade(wal->shade, wal->pal), sec->visibility, get_floor_fogpal(sec));

                pow2xsplit = 1;
#ifdef YAX_ENABLE
                if (should_clip_cfwall(x1,cy1,x0,cy0))
#endif
                polymost_domost(x1,ocy1,x0,ocy0,cy1,ocy1,cy0,ocy0);
                if (wal->cstat&8) { xtex.u = ogux; ytex.u = oguy; otex.u = oguo; }

                doeditorcheck = 0;
            }
            if (((ofy0 < fy0) || (ofy1 < fy1)) && (!((sec->floorstat&sector[nextsectnum].floorstat)&1)))
            {
                uwallptr_t nwal;
               
                if (searchit == 2)
                {
                    psectnum = sectnum;
                    pbottomwall = pwallnum = wallnum;
                    pisbottomwall = 1;
                    if ((wal->cstat&2) > 0) pbottomwall = wal->nextwall;
                    psearchstat = 0;
                    doeditorcheck = 1;
                }
                if (!(wal->cstat&2)) nwal = wal;
                else
                {
                    nwal = (uwallptr_t)&wall[wal->nextwall];
                    otex.u += (float)(nwal->xpanning - wal->xpanning) * otex.d;
                    xtex.u += (float)(nwal->xpanning - wal->xpanning) * xtex.d;
                    ytex.u += (float)(nwal->xpanning - wal->xpanning) * ytex.d;
                }
                globalpicnum = nwal->picnum; globalshade = nwal->shade; globalpal = (int32_t)((uint8_t)nwal->pal);
                globvis = globalvisibility;
                if (sector[sectnum].visibility != 0) globvis = mulscale4(globvis, (uint8_t)(sector[sectnum].visibility+16));
                globvis2 = globalvisibility2;
                if (sector[sectnum].visibility != 0) globvis2 = mulscale4(globvis2, (uint8_t)(sector[sectnum].visibility+16));
                polymost_setVisibility(globvis2);
                globalorientation = nwal->cstat;
                tileUpdatePicnum(&globalpicnum, wallnum+16384);

                int i = (!(nwal->cstat&4)) ? sector[nextsectnum].floorz : sec->ceilingz;

                // under
                calc_ypanning(i, ryp0, ryp1, x0, x1, nwal->ypanning, wal->yrepeat, !(nwal->cstat&4));

                if (wal->cstat&8) //xflip
                {
                    float const t = (float)(wal->xrepeat*8 + nwal->xpanning*2);
                    xtex.u = xtex.d*t - xtex.u;
                    ytex.u = ytex.d*t - ytex.u;
                    otex.u = otex.d*t - otex.u;
                }
                if (nwal->cstat&256) { xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; } //yflip

                if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
                    calc_and_apply_fog(fogshade(nwal->shade, nwal->pal), sec->visibility, get_floor_fogpal(sec));

                pow2xsplit = 1;
#ifdef YAX_ENABLE
                if (should_clip_cfwall(x0,fy0,x1,fy1))
#endif
                polymost_domost(x0,ofy0,x1,ofy1,ofy0,fy0,ofy1,fy1);
                if (wal->cstat&(2+8)) { otex.u = oguo; xtex.u = ogux; ytex.u = oguy; }

                doeditorcheck = 0;
            }
        }

        if ((nextsectnum < 0) || (wal->cstat&32))   //White/1-way wall
        {
            do
            {
                const int maskingOneWay = (nextsectnum >= 0 && (wal->cstat&32));

                if (searchit == 2)
                {
                    psectnum = sectnum;
                    pbottomwall = pwallnum = wallnum;
                    psearchstat = (nextsectnum < 0) ? 0 : 4;
                    doeditorcheck = 1;
                }

                if (maskingOneWay)
                {
                    vec2_t n, pos = { globalposx, globalposy };
                    if (!polymost_getclosestpointonwall(&pos, wallnum, &n) && klabs(pos.x - n.x) + klabs(pos.y - n.y) <= 128)
                        break;
                }

                globalpicnum = (nextsectnum < 0) ? wal->picnum : wal->overpicnum;

                globalshade = wal->shade;
                globalpal = wal->pal;
                globvis = (sector[sectnum].visibility != 0) ?
                          mulscale4(globalvisibility, (uint8_t)(sector[sectnum].visibility + 16)) :
                          globalvisibility;
                globvis2 = (sector[sectnum].visibility != 0) ?
                          mulscale4(globalvisibility2, (uint8_t)(sector[sectnum].visibility + 16)) :
                          globalvisibility2;
                polymost_setVisibility(globvis2);
                globalorientation = wal->cstat;
                tileUpdatePicnum(&globalpicnum, wallnum+16384);

                int i;
                int const nwcs4 = !(wal->cstat & 4);

                if (nextsectnum >= 0) { i = nwcs4 ? nextsec->ceilingz : sec->ceilingz; }
                else { i = nwcs4 ? sec->ceilingz : sec->floorz; }

                // white / 1-way
                calc_ypanning(i, ryp0, ryp1, x0, x1, wal->ypanning, wal->yrepeat, nwcs4 && !maskingOneWay);

                if (wal->cstat&8) //xflip
                {
                    float const t = (float) (wal->xrepeat*8 + wal->xpanning*2);
                    xtex.u = xtex.d*t - xtex.u;
                    ytex.u = ytex.d*t - ytex.u;
                    otex.u = otex.d*t - otex.u;
                }
                if (wal->cstat&256) { xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; } //yflip

                if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
                    calc_and_apply_fog(fogshade(wal->shade, wal->pal), sec->visibility, get_floor_fogpal(sec));

                pow2xsplit = 1;

#ifdef YAX_ENABLE
                // TODO: slopes?

                if (globalposz > sec->floorz && yax_isislandwall(wallnum, YAX_FLOOR))
                    polymost_domost(x1, fy1, x0, fy0, cy1, fy1, cy0, fy0);
                else
#endif
                    polymost_domost(x0, cy0, x1, cy1, cy0, fy0, cy1, fy1);
                doeditorcheck = 0;
            } while (0);
        }

        domostpolymethod = DAMETH_NOMASK;

        if (nextsectnum >= 0)
            if ((!(gotsector[nextsectnum>>3]&pow2char[nextsectnum&7])) && testvisiblemost(x0,x1))
                polymost_scansector(nextsectnum);
    }
}

static int32_t polymost_bunchfront(const int32_t b1, const int32_t b2)
{
    int b1f = bunchfirst[b1];
    const float x2b2 = dxb2[bunchlast[b2]];
    const float x1b1 = dxb1[b1f];

    if (nexttowardf(x1b1, x2b2) >= x2b2)
        return -1;

    int b2f = bunchfirst[b2];
    const float x1b2 = dxb1[b2f];

    if (nexttowardf(x1b2, dxb2[bunchlast[b1]]) >= dxb2[bunchlast[b1]])
        return -1;

    if (nexttowardf(x1b1, x1b2) > x1b2)
    {
        while (nexttowardf(dxb2[b2f], x1b1) <= x1b1) b2f=bunchp2[b2f];
        return wallfront(b1f, b2f);
    }

    while (nexttowardf(dxb2[b1f], x1b2) <= x1b2) b1f=bunchp2[b1f];
    return wallfront(b1f, b2f);
}

void polymost_scansector(int32_t sectnum)
{
    if (sectnum < 0) return;

    if (automapping)
        show2dsector[sectnum>>3] |= pow2char[sectnum&7];

    sectorborder[0] = sectnum;
    int sectorbordercnt = 1;
    do
    {
        sectnum = sectorborder[--sectorbordercnt];

#ifdef YAX_ENABLE
        if (scansector_collectsprites)
#endif
        for (bssize_t z=headspritesect[sectnum]; z>=0; z=nextspritesect[z])
        {
            auto const spr = (uspriteptr_t)&sprite[z];

            if ((spr->cstat & 0x8000 && !showinvisibility) || spr->xrepeat == 0 || spr->yrepeat == 0)
                continue;

            vec2_t const s = { spr->x-globalposx, spr->y-globalposy };

            if ((spr->cstat&48) ||
                (usemodels && tile2model[spr->picnum].modelid>=0) ||
                ((s.x * gcosang) + (s.y * gsinang) > 0))
            {
                if ((spr->cstat&(64+48))!=(64+16) ||
                    (usevoxels && tiletovox[spr->picnum] >= 0 && voxmodels[tiletovox[spr->picnum]]) ||
                    dmulscale6(sintable[(spr->ang+512)&2047],-s.x, sintable[spr->ang&2047],-s.y) > 0)
                    if (renderAddTsprite(z, sectnum))
                        break;
            }
        }

        gotsector[sectnum>>3] |= pow2char[sectnum&7];

        int const bunchfrst = numbunches;
        int const onumscans = numscans;
        int const startwall = sector[sectnum].wallptr;
        int const endwall   = sector[sectnum].wallnum + startwall;

        int scanfirst = numscans;

        vec2d_t p2 = { 0, 0 };

        uwallptr_t wal;
        int z;

        for (z=startwall,wal=(uwallptr_t)&wall[z]; z<endwall; z++,wal++)
        {
            auto const wal2 = (uwallptr_t)&wall[wal->point2];

            vec2d_t const fp1 = { double(wal->x - globalposx), double(wal->y - globalposy) };
            vec2d_t const fp2 = { double(wal2->x - globalposx), double(wal2->y - globalposy) };

            int const nextsectnum = wal->nextsector; //Scan close sectors

            if (nextsectnum >= 0 && !(wal->cstat&32) && sectorbordercnt < ARRAY_SSIZE(sectorborder))
#ifdef YAX_ENABLE
            if (yax_nomaskpass==0 || !yax_isislandwall(z, !yax_globalcf) || (yax_nomaskdidit=1, 0))
#endif
            if ((gotsector[nextsectnum>>3]&pow2char[nextsectnum&7]) == 0)
            {
                double const d = fp1.x*fp2.y - fp2.x*fp1.y;
                vec2d_t const p1 = { fp2.x-fp1.x, fp2.y-fp1.y };

                // this said (SCISDIST*SCISDIST*260.f), but SCISDIST is 1 and the significance of 260 isn't obvious to me
                // is 260 fudged to solve a problem, and does the problem still apply to our version of the renderer?
                if (d*d < (p1.x*p1.x + p1.y*p1.y) * 256.f)
                {
                    sectorborder[sectorbordercnt++] = nextsectnum;
                    gotsector[nextsectnum>>3] |= pow2char[nextsectnum&7];
                }
            }

            vec2d_t p1;

            if ((z == startwall) || (wall[z-1].point2 != z))
            {
                p1 = { (((fp1.y * fcosglobalang) - (fp1.x * fsinglobalang)) * (1.0/64.0)),
                       (((fp1.x * cosviewingrangeglobalang) + (fp1.y * sinviewingrangeglobalang)) * (1.0/64.0)) };
            }
            else { p1 = p2; }

            p2 = { (((fp2.y * fcosglobalang) - (fp2.x * fsinglobalang)) * (1.0/64.0)),
                   (((fp2.x * cosviewingrangeglobalang) + (fp2.y * sinviewingrangeglobalang)) * (1.0/64.0)) };

            if (numscans >= MAXWALLSB-1)
            {
                OSD_Printf("!!numscans\n");
                return;
            }

            //if wall is facing you...
            if ((p1.y >= SCISDIST || p2.y >= SCISDIST) && (nexttoward(p1.x*p2.y, p2.x*p1.y) < p2.x*p1.y))
            {
                dxb1[numscans] = (p1.y >= SCISDIST) ? float(p1.x*ghalfx/p1.y + ghalfx) : -1e32f;
                dxb2[numscans] = (p2.y >= SCISDIST) ? float(p2.x*ghalfx/p2.y + ghalfx) : 1e32f;

                if (dxb1[numscans] < xbl)
                    dxb1[numscans] = xbl;
                else if (dxb1[numscans] > xbr)
                    dxb1[numscans] = xbr;
                if (dxb2[numscans] < xbl)
                    dxb2[numscans] = xbl;
                else if (dxb2[numscans] > xbr)
                    dxb2[numscans] = xbr;

                if (nexttowardf(dxb1[numscans], dxb2[numscans]) < dxb2[numscans])
                {
                    thesector[numscans] = sectnum;
                    thewall[numscans] = z;
                    bunchp2[numscans] = numscans + 1;
                    numscans++;
                }
            }

            if ((wall[z].point2 < z) && (scanfirst < numscans))
            {
                bunchp2[numscans-1] = scanfirst;
                scanfirst = numscans;
            }
        }

        for (bssize_t z=onumscans; z<numscans; z++)
        {
            if ((wall[thewall[z]].point2 != thewall[bunchp2[z]]) || (dxb2[z] > nexttowardf(dxb1[bunchp2[z]], dxb2[z])))
            {
                bunchfirst[numbunches++] = bunchp2[z];
                bunchp2[z] = -1;
#ifdef YAX_ENABLE
                if (scansector_retfast)
                    return;
#endif
            }
        }

        for (bssize_t z=bunchfrst; z<numbunches; z++)
        {
            int zz;
            for (zz=bunchfirst[z]; bunchp2[zz]>=0; zz=bunchp2[zz]) { }
            bunchlast[z] = zz;
        }
    }
    while (sectorbordercnt > 0);
}

/*Init viewport boundary (must be 4 point convex loop):
//      (px[0],py[0]).----.(px[1],py[1])
//                  /      \
//                /          \
// (px[3],py[3]).--------------.(px[2],py[2])
*/

static void polymost_initmosts(const float * px, const float * py, int const n)
{
    if (n < 3) return;

    int32_t imin = (px[1] < px[0]);

    for (bssize_t i=n-1; i>=2; i--)
        if (px[i] < px[imin]) imin = i;

    int32_t vcnt = 1; //0 is dummy solid node

    vsp[vcnt].x = px[imin];
    vsp[vcnt].cy[0] = vsp[vcnt].fy[0] = py[imin];
    vcnt++;

    int i = imin+1, j = imin-1;
    if (i >= n) i = 0;
    if (j < 0) j = n-1;

    do
    {
        if (px[i] < px[j])
        {
            if (px[i] <= vsp[vcnt-1].x) vcnt--;
            vsp[vcnt].x = px[i];
            vsp[vcnt].cy[0] = py[i];
            int k = j+1; if (k >= n) k = 0;
            //(px[k],py[k])
            //(px[i],?)
            //(px[j],py[j])
            vsp[vcnt].fy[0] = (px[i]-px[k])*(py[j]-py[k])/(px[j]-px[k]) + py[k];
            vcnt++;
            i++; if (i >= n) i = 0;
        }
        else if (px[j] < px[i])
        {
            if (px[j] <= vsp[vcnt-1].x) vcnt--;
            vsp[vcnt].x = px[j];
            vsp[vcnt].fy[0] = py[j];
            int k = i-1; if (k < 0) k = n-1;
            //(px[k],py[k])
            //(px[j],?)
            //(px[i],py[i])
            vsp[vcnt].cy[0] = (px[j]-px[k])*(py[i]-py[k])/(px[i]-px[k]) + py[k];
            vcnt++;
            j--; if (j < 0) j = n-1;
        }
        else
        {
            if (px[i] <= vsp[vcnt-1].x) vcnt--;
            vsp[vcnt].x = px[i];
            vsp[vcnt].cy[0] = py[i];
            vsp[vcnt].fy[0] = py[j];
            vcnt++;
            i++; if (i >= n) i = 0; if (i == j) break;
            j--; if (j < 0) j = n-1;
        }
    } while (i != j);

    if (px[i] > vsp[vcnt-1].x)
    {
        vsp[vcnt].x = px[i];
        vsp[vcnt].cy[0] = vsp[vcnt].fy[0] = py[i];
        vcnt++;
    }

    domost_rejectcount = 0;

    vsp_finalize_init(vcnt);

    xbl = px[0];
    xbr = px[0];
    xbt = py[0];
    xbb = py[0];

    for (bssize_t i=n-1; i>=1; i--)
    {
        if (xbl > px[i]) xbl = px[i];
        if (xbr < px[i]) xbr = px[i];
        if (xbt > py[i]) xbt = py[i];
        if (xbb < py[i]) xbb = py[i];
    }

    gtag = vcnt;
    viewportNodeCount = vcnt;
}

void polymost_drawrooms()
{
    if (videoGetRenderMode() == REND_CLASSIC) return;

    polymost_outputGLDebugMessage(3, "polymost_drawrooms()");

    videoBeginDrawing();
    frameoffset = frameplace + windowxy1.y*bytesperline + windowxy1.x;

#ifdef YAX_ENABLE
    if (numyaxbunches==0)
#endif
    if (editstatus)
        glClear(GL_COLOR_BUFFER_BIT);

#ifdef YAX_ENABLE
    if (yax_polymostclearzbuffer)
#endif
    glClear(GL_DEPTH_BUFFER_BIT);

    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS
//        glDepthRange(0.0, 1.0); //<- this is more widely supported than glPolygonOffset

    gvrcorrection = viewingrange*(1.f/65536.f);
    if (glprojectionhacks == 2)
    {
        // calculates the extend of the zenith glitch
        float verticalfovtan = (fviewingrange * (windowxy2.y-windowxy1.y) * 5.f) / ((float)yxaspect * (windowxy2.x-windowxy1.x) * 4.f);
        float verticalfov = atanf(verticalfovtan) * (2.f / fPI);
        static constexpr float const maxhorizangle = 0.6361136f; // horiz of 199 in degrees
        float zenglitch = verticalfov + maxhorizangle - 0.95f; // less than 1 because the zenith glitch extends a bit
        if (zenglitch > 0.f)
            gvrcorrection /= (zenglitch * 2.5f) + 1.f;
    }

    //Polymost supports true look up/down :) Here, we convert horizon to angle.
    //gchang&gshang are cos&sin of this angle (respectively)
    gyxscale = ((float)xdimenscale)*(1.0f/131072.f);
    gxyaspect = ((double)xyaspect*fviewingrange)*(5.0/(65536.0*262144.0));
    gviewxrange = fviewingrange * fxdimen * (1.f/(32768.f*1024.f));
    gcosang = fcosglobalang*(1.0f/262144.f);
    gsinang = fsinglobalang*(1.0f/262144.f);
    gcosang2 = gcosang * (fviewingrange * (1.0f/65536.f));
    gsinang2 = gsinang * (fviewingrange * (1.0f/65536.f));
    ghalfx = (float)(xdimen>>1);
    ghalfy = (float)(ydimen>>1);
    grhalfxdown10 = 1.f/(ghalfx*1024.f);
    ghoriz = fix16_to_float(qglobalhoriz);
    ghorizcorrect = fix16_to_float((100-polymostcenterhoriz)*divscale16(xdimenscale, viewingrange));

    gvisibility = ((float)globalvisibility)*FOGSCALE;


    polymost_shadeInterpolate(r_shadeinterpolate);

    //global cos/sin height angle
    if (r_yshearing)
    {
        gshang  = 0.f;
        gchang  = 1.f;
        ghoriz2 = (float)(ydimen >> 1) - (ghoriz + ghorizcorrect);
    }
    else
    {
        float r = (float)(ydimen >> 1) - (ghoriz + ghorizcorrect);
        gshang  = r / Bsqrtf(r * r + ghalfx * ghalfx / (gvrcorrection * gvrcorrection));
        gchang  = Bsqrtf(1.f - gshang * gshang);
        ghoriz2 = 0.f;
    }

    ghoriz = (float)(ydimen>>1);

    resizeglcheck();
    float const ratio = 1.f/get_projhack_ratio();

    //global cos/sin tilt angle
    gctang = cosf(gtang);
    gstang = sinf(gtang);

    if (Bfabsf(gstang) < .001f)  // This hack avoids nasty precision bugs in domost()
    {
        gstang = 0.f;
        gctang = (gctang > 0.f) ? 1.f : -1.f;
    }

    if (inpreparemirror)
        gstang = -gstang;

    //Generate viewport trapezoid (for handling screen up/down)
    vec3f_t p[4] = {  { 0-1,                                        0-1+ghorizcorrect,                                  0 },
                      { (float)(windowxy2.x + 1 - windowxy1.x + 2), 0-1+ghorizcorrect,                                  0 },
                      { (float)(windowxy2.x + 1 - windowxy1.x + 2), (float)(windowxy2.y + 1 - windowxy1.y + 2)+ghorizcorrect, 0 },
                      { 0-1,                                        (float)(windowxy2.y + 1 - windowxy1.y + 2)+ghorizcorrect, 0 } };

    for (auto & v : p)
    {
        //Tilt rotation (backwards)
        vec2f_t const o = { (v.x-ghalfx)*ratio, (v.y-ghoriz)*ratio };
        vec3f_t const o2 = { o.x*gctang + o.y*gstang, o.y*gctang - o.x*gstang + ghoriz2, ghalfx / gvrcorrection };

        //Up/down rotation (backwards)
        v = { o2.x, o2.y * gchang + o2.z * gshang, o2.z * gchang - o2.y * gshang };
    }

#if !SOFTROTMAT
    if (inpreparemirror)
        gstang = -gstang;
#endif

    //Clip to SCISDIST plane
    int n = 0;

    vec3f_t p2[6];

    for (bssize_t i=0; i<4; i++)
    {
        int const j = i < 3 ? i + 1 : 0;

        if (p[i].z >= SCISDIST)
            p2[n++] = p[i];

        if ((p[i].z >= SCISDIST) != (p[j].z >= SCISDIST))
        {
            float const r = (SCISDIST - p[i].z) / (p[j].z - p[i].z);
            p2[n++] = { (p[j].x - p[i].x) * r + p[i].x, (p[j].y - p[i].y) * r + p[i].y, SCISDIST };
        }
    }

    if (n < 3) { glDepthFunc(GL_LEQUAL); videoEndDrawing(); return; }

    float sx[6], sy[6];

    for (bssize_t i = 0; i < n; i++)
    {
        float const r = (ghalfx / gvrcorrection) / p2[i].z;
        sx[i] = p2[i].x * r + ghalfx;
        sy[i] = p2[i].y * r + ghoriz;
    }

    polymost_initmosts(sx, sy, n);

#ifdef YAX_ENABLE
    if (yax_globallev != YAX_MAXDRAWS)
    {
        int i, newi;
        int32_t nodrawbak = g_nodraw;
        g_nodraw = 1;
        for (i = yax_vsp[yax_globalbunch*2][0].n; i; i=newi)
        {
            newi = yax_vsp[yax_globalbunch*2][i].n;
            if (!newi)
                break;
            polymost_domost(yax_vsp[yax_globalbunch*2][newi].x, yax_vsp[yax_globalbunch*2][i].cy[1]-DOMOST_OFFSET, yax_vsp[yax_globalbunch*2][i].x, yax_vsp[yax_globalbunch*2][i].cy[0]-DOMOST_OFFSET);
        }
        for (i = yax_vsp[yax_globalbunch*2+1][0].n; i; i=newi)
        {
            newi = yax_vsp[yax_globalbunch*2+1][i].n;
            if (!newi)
                break;
            polymost_domost(yax_vsp[yax_globalbunch*2+1][i].x, yax_vsp[yax_globalbunch*2+1][i].cy[0]+DOMOST_OFFSET, yax_vsp[yax_globalbunch*2+1][newi].x, yax_vsp[yax_globalbunch*2+1][i].cy[1]+DOMOST_OFFSET);
        }
        g_nodraw = nodrawbak;

#ifdef COMBINE_STRIPS
        i = vsp[0].n;

        do
        {
            int const ni = vsp[i].n;

            //POGO: specially treat the viewport nodes so that we will never end up in a situation where we accidentally access the sentinel node
            if (ni >= viewportNodeCount)
            {
                if (Bfabsf(vsp[i].cy[1]-vsp[ni].cy[0]) < 0.1f && Bfabsf(vsp[i].fy[1]-vsp[ni].fy[0]) < 0.1f)
                {
                    float const dx = 1.f/(vsp[ni].x-vsp[i].x);
                    float const dx2 = 1.f/(vsp[vsp[ni].n].x-vsp[i].x);
                    float const cslop[2] = { vsp[i].cy[1]-vsp[i].cy[0], vsp[ni].cy[1]-vsp[i].cy[0] };
                    float const fslop[2] = { vsp[i].fy[1]-vsp[i].fy[0], vsp[ni].fy[1]-vsp[i].fy[0] };

                    if (Bfabsf(cslop[0]*dx-cslop[1]*dx2) < 0.001f && Bfabsf(fslop[0]*dx-fslop[1]*dx2) < 0.001f)
                    {
                        vsmerge(i, ni);
                        continue;
                    }
                }
            }
            i = ni;
        }
        while (i);
#endif
    }
    //else if (!g_nodraw) { videoEndDrawing(); return; }
#endif
    
    doeditorcheck = 0;

    if (searchit >= 1)
    {
        vec2f_t const o = { (searchx-ghalfx)*ratio, (searchy-ghoriz)*ratio };
        vec3f_t const o2 = { o.x*gctang + o.y*gstang, o.y*gctang - o.x*gstang + ghoriz2, (ghalfx / gvrcorrection) };
        vec3f_t const v = { o2.x, o2.y * gchang + o2.z * gshang, o2.z * gchang - o2.y * gshang };
        float const r = (ghalfx / gvrcorrection) / v.z;
        fsearchx = v.x * r + ghalfx;
        fsearchy = v.y * r + ghoriz;
        fsearchz = 0.f;
        polymost_editorfunc();
    }

    polymost_updaterotmat();

    numscans = numbunches = 0;

    // MASKWALL_BAD_ACCESS
    // Fixes access of stale maskwall[maskwallcnt] (a "scan" index, in BUILD lingo):
    maskwallcnt = 0;

    // NOTE: globalcursectnum has been already adjusted in ADJUST_GLOBALCURSECTNUM.
    Bassert((unsigned)globalcursectnum < MAXSECTORS);
    polymost_scansector(globalcursectnum);

    grhalfxdown10x = grhalfxdown10;

    if (inpreparemirror)
    {
        // see engine.c: INPREPAREMIRROR_NO_BUNCHES
        if (numbunches > 0)
        {
            grhalfxdown10x = -grhalfxdown10;
            polymost_drawalls(0);
            numbunches--;
            bunchfirst[0] = bunchfirst[numbunches];
            bunchlast[0] = bunchlast[numbunches];
        } else
        {
            inpreparemirror = 0;
        }
    }

    while (numbunches > 0)
    {
        Bmemset(ptempbuf,0,numbunches+3); ptempbuf[0] = 1;

        int32_t closest = 0;              //Almost works, but not quite :(

        for (bssize_t i=1; i<numbunches; ++i)
        {
            int const bnch = polymost_bunchfront(i,closest); if (bnch < 0) continue;
            ptempbuf[i] = 1;
            if (!bnch) { ptempbuf[closest] = 1; closest = i; }
        }
        for (bssize_t i=0; i<numbunches; ++i) //Double-check
        {
            if (ptempbuf[i]) continue;
            int const bnch = polymost_bunchfront(i,closest); if (bnch < 0) continue;
            ptempbuf[i] = 1;
            if (!bnch) { ptempbuf[closest] = 1; closest = i; i = 0; }
        }

        polymost_drawalls(closest);

        if (automapping)
        {
            for (int z=bunchfirst[closest]; z>=0; z=bunchp2[z])
                show2dwall[thewall[z]>>3] |= pow2char[thewall[z]&7];
        }

        numbunches--;
        bunchfirst[closest] = bunchfirst[numbunches];
        bunchlast[closest] = bunchlast[numbunches];
    }

    glDepthFunc(GL_LEQUAL); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS
//        glDepthRange(0.0, 1.0); //<- this is more widely supported than glPolygonOffset
    polymost_identityrotmat();

    videoEndDrawing();
}

static void polymost_drawmaskwallinternal(int32_t wallIndex)
{
    auto const wal = (uwallptr_t)&wall[wallIndex];
    auto const wal2 = (uwallptr_t)&wall[wal->point2];
    int32_t const sectnum = wall[wal->nextwall].nextsector;
    auto const sec = (usectorptr_t)&sector[sectnum];

//    if (wal->nextsector < 0) return;
    // Without MASKWALL_BAD_ACCESS fix:
    // wal->nextsector is -1, WGR2 SVN Lochwood Hollow (Til' Death L1)  (or trueror1.map)

    auto const nsec = (usectorptr_t)&sector[wal->nextsector];

    polymost_outputGLDebugMessage(3, "polymost_drawmaskwallinternal(wallIndex:%d)", wallIndex);

    globalpicnum = wal->overpicnum;
    if ((uint32_t)globalpicnum >= MAXTILES)
        globalpicnum = 0;

    globalorientation = (int32_t)wal->cstat;
    tileUpdatePicnum(&globalpicnum, (int16_t)wallIndex+16384);

    globvis = globalvisibility;
    globvis = (sector[sectnum].visibility != 0) ? mulscale4(globvis, (uint8_t)(sector[sectnum].visibility + 16)) : globalvisibility;

    globvis2 = globalvisibility2;
    globvis2 = (sector[sectnum].visibility != 0) ? mulscale4(globvis2, (uint8_t)(sector[sectnum].visibility + 16)) : globalvisibility2;
    polymost_setVisibility(globvis2);

    globalshade = (int32_t)wal->shade;
    globalpal = (int32_t)((uint8_t)wal->pal);

    vec2f_t s0 = { (float)(wal->x-globalposx), (float)(wal->y-globalposy) };
    vec2f_t p0 = { s0.y*gcosang - s0.x*gsinang, s0.x*gcosang2 + s0.y*gsinang2 };

    vec2f_t s1 = { (float)(wal2->x-globalposx), (float)(wal2->y-globalposy) };
    vec2f_t p1 = { s1.y*gcosang - s1.x*gsinang, s1.x*gcosang2 + s1.y*gsinang2 };

    if ((p0.y < SCISDIST) && (p1.y < SCISDIST)) return;

    //Clip to close parallel-screen plane
    vec2f_t const op0 = p0;

    float t0 = 0.f;

    if (p0.y < SCISDIST)
    {
        t0 = (SCISDIST - p0.y) / (p1.y - p0.y);
        p0 = { (p1.x - p0.x) * t0 + p0.x, SCISDIST };
    }

    float t1 = 1.f;

    if (p1.y < SCISDIST)
    {
        t1 = (SCISDIST - op0.y) / (p1.y - op0.y);
        p1 = { (p1.x - op0.x) * t1 + op0.x, SCISDIST };
    }

    int32_t m0 = (int32_t)((wal2->x - wal->x) * t0 + wal->x);
    int32_t m1 = (int32_t)((wal2->y - wal->y) * t0 + wal->y);
    int32_t cz[4], fz[4];
    getzsofslope(sectnum, m0, m1, &cz[0], &fz[0]);
    getzsofslope(wal->nextsector, m0, m1, &cz[1], &fz[1]);
    m0 = (int32_t)((wal2->x - wal->x) * t1 + wal->x);
    m1 = (int32_t)((wal2->y - wal->y) * t1 + wal->y);
    getzsofslope(sectnum, m0, m1, &cz[2], &fz[2]);
    getzsofslope(wal->nextsector, m0, m1, &cz[3], &fz[3]);

    float ryp0 = 1.f/p0.y;
    float ryp1 = 1.f/p1.y;

    //Generate screen coordinates for front side of wall
    float const x0 = ghalfx*p0.x*ryp0 + ghalfx;
    float const x1 = ghalfx*p1.x*ryp1 + ghalfx;
    if (x1 <= x0) return;

    ryp0 *= gyxscale; ryp1 *= gyxscale;

    xtex.d = (ryp0-ryp1)*gxyaspect / (x0-x1);
    ytex.d = 0;
    otex.d = ryp0*gxyaspect - xtex.d*x0;

    //gux*x0 + guo = t0*wal->xrepeat*8*yp0
    //gux*x1 + guo = t1*wal->xrepeat*8*yp1
    xtex.u = (t0*ryp0 - t1*ryp1)*gxyaspect*(float)wal->xrepeat*8.f / (x0-x1);
    otex.u = t0*ryp0*gxyaspect*(float)wal->xrepeat*8.f - xtex.u*x0;
    otex.u += (float)wal->xpanning*otex.d;
    xtex.u += (float)wal->xpanning*xtex.d;
    ytex.u = 0;

    // mask
    calc_ypanning((!(wal->cstat & 4)) ? max(nsec->ceilingz, sec->ceilingz) : min(nsec->floorz, sec->floorz), ryp0, ryp1,
                  x0, x1, wal->ypanning, wal->yrepeat, 0);

    if (wal->cstat&8) //xflip
    {
        float const t = (float)(wal->xrepeat*8 + wal->xpanning*2);
        xtex.u = xtex.d*t - xtex.u;
        ytex.u = ytex.d*t - ytex.u;
        otex.u = otex.d*t - otex.u;
    }
    if (wal->cstat&256) { xtex.v = -xtex.v; ytex.v = -ytex.v; otex.v = -otex.v; } //yflip

    int method = DAMETH_MASK | DAMETH_WALL;

    if (wal->cstat & 128)
        method = DAMETH_WALL | (((wal->cstat & 512)) ? DAMETH_TRANS2 : DAMETH_TRANS1);

#ifdef NEW_MAP_FORMAT
    uint8_t const blend = wal->blend;
#else
    uint8_t const blend = wallext[wallIndex].blend;
#endif
    handle_blend(!!(wal->cstat & 128), blend, !!(wal->cstat & 512));

    drawpoly_alpha = 0.f;
    drawpoly_blend = blend;

    if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART)))
        calc_and_apply_fog(fogshade(wal->shade, wal->pal), sec->visibility, get_floor_fogpal(sec));

    float const csy[4] = { ((float)(cz[0] - globalposz)) * ryp0 + ghoriz,
                           ((float)(cz[1] - globalposz)) * ryp0 + ghoriz,
                           ((float)(cz[2] - globalposz)) * ryp1 + ghoriz,
                           ((float)(cz[3] - globalposz)) * ryp1 + ghoriz };

    float const fsy[4] = { ((float)(fz[0] - globalposz)) * ryp0 + ghoriz,
                           ((float)(fz[1] - globalposz)) * ryp0 + ghoriz,
                           ((float)(fz[2] - globalposz)) * ryp1 + ghoriz,
                           ((float)(fz[3] - globalposz)) * ryp1 + ghoriz };

    //Clip 2 quadrilaterals
    //               /csy3
    //             /   |
    // csy0------/----csy2
    //   |     /xxxxxxx|
    //   |   /xxxxxxxxx|
    // csy1/xxxxxxxxxxx|
    //   |xxxxxxxxxxx/fsy3
    //   |xxxxxxxxx/   |
    //   |xxxxxxx/     |
    // fsy0----/------fsy2
    //   |   /
    // fsy1/

    vec2f_t dpxy[8] = { { x0, csy[1] }, { x1, csy[3] }, { x1, fsy[3] }, { x0, fsy[1] } };

    //Clip to (x0,csy[0])-(x1,csy[2])

    vec2f_t dp2[8];

    int n2 = 0;
    t1 = -((dpxy[0].x - x0) * (csy[2] - csy[0]) - (dpxy[0].y - csy[0]) * (x1 - x0));

    for (bssize_t i=0; i<4; i++)
    {
        int j = i + 1;

        if (j >= 4)
            j = 0;

        t0 = t1;
        t1 = -((dpxy[j].x - x0) * (csy[2] - csy[0]) - (dpxy[j].y - csy[0]) * (x1 - x0));

        if (t0 >= 0)
            dp2[n2++] = dpxy[i];

        if ((t0 >= 0) != (t1 >= 0) && (t0 <= 0) != (t1 <= 0))
        {
            float const r = t0 / (t0 - t1);
            dp2[n2++] = { (dpxy[j].x - dpxy[i].x) * r + dpxy[i].x, (dpxy[j].y - dpxy[i].y) * r + dpxy[i].y };
        }
    }

    if (n2 < 3)
        return;

    //Clip to (x1,fsy[2])-(x0,fsy[0])
    t1 = -((dp2[0].x - x1) * (fsy[0] - fsy[2]) - (dp2[0].y - fsy[2]) * (x0 - x1));
    int n = 0;

    for (bssize_t i = 0, j = 1; i < n2; j = ++i + 1)
    {
        if (j >= n2)
            j = 0;

        t0 = t1;
        t1 = -((dp2[j].x - x1) * (fsy[0] - fsy[2]) - (dp2[j].y - fsy[2]) * (x0 - x1));

        if (t0 >= 0)
            dpxy[n++] = dp2[i];

        if ((t0 >= 0) != (t1 >= 0) && (t0 <= 0) != (t1 <= 0))
        {
            float const r = t0 / (t0 - t1);
            dpxy[n++] = { (dp2[j].x - dp2[i].x) * r + dp2[i].x, (dp2[j].y - dp2[i].y) * r + dp2[i].y };
        }
    }

    if (n < 3)
        return;

    pow2xsplit = 0;
    skyclamphack = 0;

    polymost_updaterotmat();
    if (searchit >= 1)
    {
        psectnum = sectnum;
        pbottomwall = pwallnum = wallIndex;
        psearchstat = 4;
        doeditorcheck = 1;
    }
    polymost_drawpoly(dpxy, n, method);
    doeditorcheck = 0;
    polymost_identityrotmat();
}

void polymost_drawmaskwall(int32_t damaskwallcnt)
{
    int const z = maskwall[damaskwallcnt];
    polymost_drawmaskwallinternal(thewall[z]);
}

void polymost_prepareMirror(int32_t dax, int32_t day, int32_t daz, fix16_t daang, fix16_t dahoriz, int16_t mirrorWall)
{
    polymost_outputGLDebugMessage(3, "polymost_prepareMirror(%u)", mirrorWall);

    //POGO: prepare necessary globals for drawing, as we intend to call this outside of drawrooms
    gvrcorrection = viewingrange*(1.f/65536.f);
    if (glprojectionhacks == 2)
    {
        // calculates the extend of the zenith glitch
        float verticalfovtan = (fviewingrange * (windowxy2.y-windowxy1.y) * 5.f) / ((float)yxaspect * (windowxy2.x-windowxy1.x) * 4.f);
        float verticalfov = atanf(verticalfovtan) * (2.f / fPI);
        static constexpr float const maxhorizangle = 0.6361136f; // horiz of 199 in degrees
        float zenglitch = verticalfov + maxhorizangle - 0.95f; // less than 1 because the zenith glitch extends a bit
        if (zenglitch > 0.f)
            gvrcorrection /= (zenglitch * 2.5f) + 1.f;
    }

    set_globalpos(dax, day, daz);
    set_globalang(daang);
    globalhoriz = mulscale16(fix16_to_int(dahoriz)-100,divscale16(xdimenscale,viewingrange))+(ydimen>>1);
    qglobalhoriz = mulscale16(dahoriz-F16(100), divscale16(xdimenscale, viewingrange))+fix16_from_int(ydimen>>1);
    gyxscale = ((float)xdimenscale)*(1.0f/131072.f);
    gxyaspect = ((double)xyaspect*fviewingrange)*(5.0/(65536.0*262144.0));
    gviewxrange = fviewingrange * fxdimen * (1.f/(32768.f*1024.f));
    gcosang = fcosglobalang*(1.0f/262144.f);
    gsinang = fsinglobalang*(1.0f/262144.f);
    gcosang2 = gcosang * (fviewingrange * (1.0f/65536.f));
    gsinang2 = gsinang * (fviewingrange * (1.0f/65536.f));
    ghalfx = (float)(xdimen>>1);
    ghalfy = (float)(ydimen>>1);
    grhalfxdown10 = 1.f/(ghalfx*1024.f);
    ghoriz = fix16_to_float(qglobalhoriz);
    ghorizcorrect = fix16_to_float((100-polymostcenterhoriz)*divscale16(xdimenscale, viewingrange));
    gvisibility = ((float)globalvisibility)*FOGSCALE;
    resizeglcheck();
    if (r_yshearing)
    {
        gshang  = 0.f;
        gchang  = 1.f;
        ghoriz2 = (float)(ydimen >> 1) - (ghoriz+ghorizcorrect);
    }
    else
    {
        float r = (float)(ydimen >> 1) - (ghoriz+ghorizcorrect);
        gshang  = r / Bsqrtf(r * r + ghalfx * ghalfx / (gvrcorrection * gvrcorrection));
        gchang  = Bsqrtf(1.f - gshang * gshang);
        ghoriz2 = 0.f;
    }
    ghoriz = (float)(ydimen>>1);
    gctang = cosf(gtang);
    gstang = sinf(gtang);
    if (Bfabsf(gstang) < .001f)
    {
        gstang = 0.f;
        gctang = (gctang > 0.f) ? 1.f : -1.f;
    }
    grhalfxdown10x = grhalfxdown10;

    //POGO: write the mirror region to the stencil buffer to allow showing mirrors & skyboxes at the same time
    glEnable(GL_STENCIL_TEST);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    polymost_drawmaskwallinternal(mirrorWall);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);

    //POGO: render only to the mirror region
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void polymost_completeMirror()
{
    polymost_outputGLDebugMessage(3, "polymost_completeMirror()");
    glDisable(GL_STENCIL_TEST);
}

typedef struct
{
    uint32_t wrev;
    uint32_t srev;
    int16_t wall;
    int8_t wdist;
    int8_t filler;
} wallspriteinfo_t;

wallspriteinfo_t wsprinfo[MAXSPRITES];

void Polymost_prepare_loadboard(void)
{
    Bmemset(wsprinfo, 0, sizeof(wsprinfo));
}

static inline int32_t polymost_findwall(tspritetype const * const tspr, vec2_t const * const tsiz, int32_t * rd)
{
    int32_t dist = 4, closest = -1;
    auto const sect = (usectortype  * )&sector[tspr->sectnum];
    vec2_t n;

    for (bssize_t i=sect->wallptr; i<sect->wallptr + sect->wallnum; i++)
    {
        if ((wall[i].nextsector == -1 || ((sector[wall[i].nextsector].ceilingz > (tspr->z - ((tsiz->y * tspr->yrepeat) << 2))) ||
             sector[wall[i].nextsector].floorz < tspr->z)) && !polymost_getclosestpointonwall((const vec2_t *) tspr, i, &n))
        {
            int const dst = klabs(tspr->x - n.x) + klabs(tspr->y - n.y);

            if (dst <= dist)
            {
                dist = dst;
                closest = i;
            }
        }
    }

    *rd = dist;

    return closest;
}

int32_t polymost_lintersect(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                            int32_t x3, int32_t y3, int32_t x4, int32_t y4)
{
    // p1 to p2 is a line segment
    int32_t const x21 = x2 - x1, x34 = x3 - x4;
    int32_t const y21 = y2 - y1, y34 = y3 - y4;
    int32_t const bot = x21 * y34 - y21 * x34;

    if (!bot)
        return 0;

    int32_t const x31 = x3 - x1, y31 = y3 - y1;
    int32_t const topt = x31 * y34 - y31 * x34;

    int rv = 1;

    if (bot > 0)
    {
        if ((unsigned)topt >= (unsigned)bot)
            rv = 0;

        int32_t topu = x21 * y31 - y21 * x31;

        if ((unsigned)topu >= (unsigned)bot)
            rv = 0;
    }
    else
    {
        if ((unsigned)topt <= (unsigned)bot)
            rv = 0;

        int32_t topu = x21 * y31 - y21 * x31;

        if ((unsigned)topu <= (unsigned)bot)
            rv = 0;
    }

    return rv;
}

#define TSPR_OFFSET_FACTOR .000008f
#define TSPR_OFFSET(tspr) ((TSPR_OFFSET_FACTOR + ((tspr->owner != -1 ? tspr->owner & 63 : 1) * TSPR_OFFSET_FACTOR)) * (float)sepdist(globalposx - tspr->x, globalposy - tspr->y, globalposz - tspr->z) * 0.025f)

void polymost2_drawsprite(int32_t snum)
{
    auto const tspr = tspriteptr[snum];

    if (EDUKE32_PREDICT_FALSE(bad_tspr(tspr)))
        return;

    usectorptr_t sec;

    int32_t spritenum = tspr->owner;

    tileUpdatePicnum(&tspr->picnum, spritenum + 32768);

    globalpicnum = tspr->picnum;
    globalshade = tspr->shade;
    globalpal = tspr->pal;
    globalorientation = tspr->cstat;
    globvis = globalvisibility;

    if (sector[tspr->sectnum].visibility != 0)
        globvis = mulscale4(globvis, (uint8_t)(sector[tspr->sectnum].visibility + 16));

    vec2f_t off = { 0.f, 0.f };

    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_SLAB)  // only non-voxel sprites should do this
    {
        int const flag = usehightile && h_xsize[globalpicnum];
        off.x = (float)((int32_t)tspr->xoffset + (flag ? h_xoffs[globalpicnum] : picanm[globalpicnum].xofs));
        off.y = (float)((int32_t)tspr->yoffset + (flag ? h_yoffs[globalpicnum] : picanm[globalpicnum].yofs));
    }

    int32_t method = DAMETH_MASK | DAMETH_CLAMPED;

    if (tspr->cstat & CSTAT_SPRITE_TRANSLUCENT)
        method = DAMETH_CLAMPED | ((tspr->cstat & CSTAT_SPRITE_TRANSLUCENT_INVERT) ? DAMETH_TRANS2 : DAMETH_TRANS1);

    handle_blend(!!(tspr->cstat & CSTAT_SPRITE_TRANSLUCENT), tspr->blend, !!(tspr->cstat & CSTAT_SPRITE_TRANSLUCENT_INVERT));

    drawpoly_alpha = spriteext[spritenum].alpha;
    drawpoly_blend = tspr->blend;

    sec = (usectorptr_t)&sector[tspr->sectnum];

    polymost2_calc_fog(fogshade(globalshade, globalpal), sec->visibility, get_floor_fogpal(sec));

    //POGOTODO: this while is an if statement
    while (!(spriteext[spritenum].flags & SPREXT_NOTMD))
    {
    	//POGOTODO: switch these to if/else for readability and rearrange for performance
        if (usemodels && tile2model[Ptile2tile(tspr->picnum, tspr->pal)].modelid >= 0 &&
            tile2model[Ptile2tile(tspr->picnum, tspr->pal)].framenum >= 0)
        {
            if (polymost_mddraw(tspr)) return;
            break;  // else, render as flat sprite
        }

        if (usevoxels && (tspr->cstat & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_SLAB && tiletovox[tspr->picnum] >= 0 && voxmodels[tiletovox[tspr->picnum]])
        {
            if (polymost_voxdraw(voxmodels[tiletovox[tspr->picnum]], tspr)) return;
            break;  // else, render as flat sprite
        }

        if ((tspr->cstat & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_SLAB && voxmodels[tspr->picnum])
        {
            polymost_voxdraw(voxmodels[tspr->picnum], tspr);
            return;
        }

        break;
    }

    //POGO: some comments seem to indicate that spinning sprites were intended to be supported before the
    //      decision was made to implement that behaviour with voxels.
    //      Skip SLAB aligned sprites when not rendering as voxels.
    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_SLAB)
    {
        return;
    }

    vec2_t pos = tspr->pos.vec2;

    if (spriteext[spritenum].flags & SPREXT_AWAY1)
    {
        pos.x += (sintable[(tspr->ang + 512) & 2047] >> 13);
        pos.y += (sintable[(tspr->ang) & 2047] >> 13);
    }
    else if (spriteext[spritenum].flags & SPREXT_AWAY2)
    {
        pos.x -= (sintable[(tspr->ang + 512) & 2047] >> 13);
        pos.y -= (sintable[(tspr->ang) & 2047] >> 13);
    }

    vec2_16_t const oldsiz = tilesiz[globalpicnum];
    vec2_t tsiz = { oldsiz.x, oldsiz.y };

    if (usehightile && h_xsize[globalpicnum])
    {
        tsiz.x = h_xsize[globalpicnum];
        tsiz.y = h_ysize[globalpicnum];
    }

    if (tsiz.x <= 0 || tsiz.y <= 0)
        return;

    vec2f_t const ftsiz = { (float) tsiz.x, (float) tsiz.y };

    //POGOTODO: some of these cases where we return could be done further up in order to skip doing throw away computation
    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT_FLOOR) &&
        (globalorientation & CSTAT_SPRITE_ONE_SIDED) != 0 &&
        (globalposz > tspr->z) == (!(globalorientation & CSTAT_SPRITE_YFLIP)))
    {
        return;
    }

    //POGOTODO: in polymost1 any sprites that are too close are pre-clipped here before any calculation

    tilesiz[globalpicnum].x = tsiz.x;
    tilesiz[globalpicnum].y = tsiz.y;

    float texScale[2] = {1.0f, -1.0f};
    float texOffset[2] = {((float) (spriteext[spritenum].xpanning) * (1.0f / 255.f)),
                          ((float) (spriteext[spritenum].ypanning) * (1.0f / 255.f))};

    float transformMatrix[4*4] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

    float modelViewMatrix[4*4] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

    float f = (65536.f*512.f) / (fxdimen*viewingrange);
    float g = 32.f / (fxdimen*gxyaspect);

    float horzScale = ftsiz.x*(1.f/64.f);
    float vertScale = ftsiz.y*(1.f/64.f);

    horzScale *= ((float)tspr->xrepeat) * (1.f/64.f);
    vertScale *= ((float)tspr->yrepeat) * (1.f/64.f);

    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT)==CSTAT_SPRITE_ALIGNMENT_FACING)
    {
        horzScale *= 256.f/320.f;
    }
    else if ((globalorientation & CSTAT_SPRITE_ALIGNMENT)==CSTAT_SPRITE_ALIGNMENT_FLOOR)
    {
        //POGOTODO: fix floor sprites to be scaled up slightly by the right amount, and note their tex is slightly clipped on the leading edges
        vertScale += 1.f/320.f;
    }

    horzScale *= f;
    vertScale *= g;

    //handle sprite flipping
    horzScale *= -2.f*((globalorientation & CSTAT_SPRITE_XFLIP) != 0) + 1.f;
    vertScale *= -2.f*(((globalorientation & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_FLOOR) &
                       ((globalorientation & CSTAT_SPRITE_YFLIP) != 0)) + 1.f;

    //POGOTODO: replace this with simply using off.x and a different float for z offsets
    //          switching that over should fix floor sprite offsets so that they flip properly when yflip/xflip is applied
    //handle orientation offsets
    vec2f_t orientationOffset = {0.f, 0.f};
    vec3f_t offs = { 0.f, 0.f, 0.f };

    off.x = 0.2f * ((float)tspr->xrepeat) * (((float) off.x) + (tsiz.x & 1)*0.5f*(((globalorientation & CSTAT_SPRITE_XFLIP) == 0)*-2.f + 1.f));
    off.y = 4.f * ((float)tspr->yrepeat) * (((float) off.y) + (((globalorientation & CSTAT_SPRITE_YCENTER) != 0) & tsiz.y & 1)*0.5f);

    int16_t angle = globalang;
    float combinedClipScale = 1.f;
    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT)==CSTAT_SPRITE_ALIGNMENT_FACING)
    {
        int const ang = (getangle(tspr->x - globalposx, tspr->y - globalposy) + 1024) & 2047;
        float const foffs = TSPR_OFFSET(tspr);
        offs = { (float) (sintable[(ang + 512) & 2047] >> 6) * foffs,
                 (float) (sintable[(ang) & 2047] >> 6) * foffs,
                 0.f};
    }
    else if ((globalorientation & CSTAT_SPRITE_ALIGNMENT)==CSTAT_SPRITE_ALIGNMENT_WALL)
    {
        angle = (tspr->ang+1024)&2047;
        /*float const foffs = TSPR_OFFSET(tspr);
        offs = { (float) (sintable[(tspr->ang + 512) & 2047] >> 6) * foffs,
                 (float) (sintable[(tspr->ang) & 2047] >> 6) * foffs};*/

        //POGOTODO: For now, just handle this exactly the same way as in polymost1.
        //          Eventually, I should change how all sprites avoid z-fighting by offsetting the z-buffer depth
        //          rather than offsetting the entire object in space.
        vec2f_t const extent = { (float)tspr->xrepeat * (float)sintable[(tspr->ang) & 2047] * (1.0f / 65536.f),
                                 (float)tspr->xrepeat * (float)sintable[(tspr->ang + 1536) & 2047] * (1.0f / 65536.f) };
        //POGOTODO: this needs to be calculated before I make my adjustments to off.x above!
        float f = (float)(tsiz.x >> 1) + (float)off.x;
        vec2f_t const vf = { extent.x * f, extent.y * f };

        int32_t const s = tspr->owner;
        int32_t walldist = 1;
        int32_t w = (s == -1) ? -1 : wsprinfo[s].wall;

        // find the wall most likely to be what the sprite is supposed to be ornamented against
        // this is really slow, so cache the result
        if (s == -1 || !wsprinfo[s].wall || (spritechanged[s] != wsprinfo[s].srev) ||
            (w != -1 && wallchanged[w] != wsprinfo[s].wrev))
        {
            w = polymost_findwall(tspr, &tsiz, &walldist);

            if (s != -1)
            {
                wallspriteinfo_t *ws = &wsprinfo[s];
                ws->wall = w;

                if (w != -1)
                {
                    ws->wdist = walldist;
                    ws->wrev = wallchanged[w];
                    ws->srev = spritechanged[s];
                }
            }
        }
        else if (s != -1)
            walldist = wsprinfo[s].wdist;

        // detect if the sprite is either on the wall line or the wall line and sprite intersect
        if (w != -1)
        {
            vec2_t v = { /*Blrintf(vf.x)*/(int)vf.x, /*Blrintf(vf.y)*/(int)vf.y };

            if (walldist <= 2 || ((pos.x - v.x) + (pos.x + v.x)) == (wall[w].x + POINT2(w).x) ||
                ((pos.y - v.y) + (pos.y + v.y)) == (wall[w].y + POINT2(w).y) ||
                polymost_lintersect(pos.x - v.x, pos.y - v.y, pos.x + v.x, pos.y + v.y, wall[w].x, wall[w].y,
                                    POINT2(w).x, POINT2(w).y))
            {
                int32_t const ang = getangle(wall[w].x - POINT2(w).x, wall[w].y - POINT2(w).y);
                float const foffs = TSPR_OFFSET(tspr);
                offs = { -(float)(sintable[(ang + 1024) & 2047] >> 6) * foffs,
                         -(float)(sintable[(ang + 512) & 2047] >> 6) * foffs,
                         0.f};
            }
        }

        //POGO: for full compatibility, facing sprites should also clip similarly (see polymost_drawsprite())
        // Clip sprites to ceilings/floors when no parallaxing
        float fullCenterYOff = off.y + (((globalorientation & CSTAT_SPRITE_YCENTER) != 0) * 2.f)
                                            * ftsiz.y * ((float)tspr->yrepeat);
        if ((!(sector[tspr->sectnum].ceilingstat & 1)) &&
            sector[tspr->sectnum].ceilingz > tspr->z + fullCenterYOff - ((tspr->yrepeat * tsiz.y) << 2))
        {
            float clipScale = ((float) (tspr->z + fullCenterYOff - sector[tspr->sectnum].ceilingz))/((float)((tspr->yrepeat * tsiz.y) << 2));
            if (clipScale <= 0.f)
            {
                //don't draw sprites fully clipped by the ceiling
                return;
            }

            texScale[1] *= clipScale;
            texOffset[1] += (1.f-clipScale)*(-1.f*((globalorientation & CSTAT_SPRITE_YFLIP) == CSTAT_SPRITE_YFLIP));
            vertScale *= clipScale;
            combinedClipScale *= clipScale;
        }
        if ((!(sector[tspr->sectnum].floorstat & 1)) &&
            sector[tspr->sectnum].floorz < tspr->z + fullCenterYOff)
        {
            float span = ((tspr->yrepeat * tsiz.y) << 2) - (tspr->z + fullCenterYOff - sector[tspr->sectnum].floorz);
            float clipScale = span/((float)((tspr->yrepeat * tsiz.y) << 2));
            if (clipScale <= 0.f)
            {
                //don't draw sprites fully clipped by the floor
                return;
            }

            texScale[1] *= clipScale;
            texOffset[1] += (1.f-clipScale)*(-1.f*((globalorientation & CSTAT_SPRITE_YFLIP) != CSTAT_SPRITE_YFLIP));
            vertScale *= clipScale;
            combinedClipScale *= clipScale;
            off.y += (float) (((tspr->yrepeat * tsiz.y) << 2) - span);
        }
        if (globalorientation & CSTAT_SPRITE_YCENTER)
        {
            combinedClipScale = 1.f;
        }
    }

    off.x *= ((float) ((globalorientation & CSTAT_SPRITE_XFLIP) != 0))*-2.f + 1.f;
    off.y *= ((float) (((globalorientation & CSTAT_SPRITE_ALIGNMENT) != CSTAT_SPRITE_ALIGNMENT_FACING) &
                       ((globalorientation & CSTAT_SPRITE_YFLIP) != 0)))*-2.f + 1.f;

    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT)==CSTAT_SPRITE_ALIGNMENT_FLOOR)
    {
        vertScale = -vertScale;
        orientationOffset.x += ftsiz.y*((float) tspr->yrepeat)*(1.f/8.f);

        // unfortunately, offsetting by only 1 isn't enough on most Android devices
        if (tspr->z == sec->ceilingz || tspr->z == sec->ceilingz + 1)
            tspr->z = sec->ceilingz + 2, orientationOffset.y += (tspr->owner & 31);

        if (tspr->z == sec->floorz || tspr->z == sec->floorz - 1)
            tspr->z = sec->floorz - 2, orientationOffset.y -= ((tspr->owner & 31));

        angle = tspr->ang;
    }
    else
    {
        off.y -= (((globalorientation & CSTAT_SPRITE_YCENTER) != 0) * 2.f +
                  ((globalorientation & CSTAT_SPRITE_YFLIP) != 0)*-4.f)
                      * combinedClipScale * ftsiz.y * ((float)tspr->yrepeat);
    }

    vec3f_t a0;
    a0.x = ((float)(pos.y-globalposy)+offs.y) * -(1.f/1024.f)*-f;
    a0.y = ((float)(pos.x-globalposx)+offs.x) * (1.f/1024.f)*f;
    a0.z = ((float)(tspr->z-globalposz)+offs.z) * -(1.f/16384.f)*g;
    orientationOffset.x *= -(1.f/1024.f)*-f;
    orientationOffset.y *= -(1.f/16384.f)*g;
    calcmat(a0, &orientationOffset, f, modelViewMatrix, angle);

    if ((globalorientation & CSTAT_SPRITE_ALIGNMENT)==CSTAT_SPRITE_ALIGNMENT_FLOOR)
    {
        float temp = modelViewMatrix[4]; modelViewMatrix[4] = modelViewMatrix[8]*16.f; modelViewMatrix[8] = -temp*(1.f/16.f);
        temp = modelViewMatrix[5]; modelViewMatrix[5] = modelViewMatrix[9]*16.f; modelViewMatrix[9] = -temp*(1.f/16.f);
        temp = modelViewMatrix[6]; modelViewMatrix[6] = modelViewMatrix[10]*16.f; modelViewMatrix[10] = -temp*(1.f/16.f);
    }

    // mirrors
    if (grhalfxdown10x < 0)
    {
        modelViewMatrix[0] = -modelViewMatrix[0]; modelViewMatrix[4] = -modelViewMatrix[4]; modelViewMatrix[8] = -modelViewMatrix[8]; modelViewMatrix[12] = -modelViewMatrix[12];
    }

    float ratio = 1.0f/get_projhack_ratio();
    float projectionMatrix[4*4] =
        {
            fydimen * ratio,    0.0f,  1.0f,            0.0f,
                       0.0f, fxdimen,  1.0f,            0.0f,
                       0.0f,    0.0f,  1.0f, fydimen * ratio,
                       0.0f,    0.0f, -1.0f,            0.0f
        };

    float scaleMatrix[4*4] =
        {
            horzScale,      0.0f,  0.0f, 0.0f,
                 0.0f, vertScale,  0.0f, 0.0f,
                 0.0f,      0.0f,  1.0f, 0.0f,
                 0.0f,      0.0f,  0.0f, 1.0f
        };
    float offsetMatrix[4*4] =
        {
                             1.0f,                         0.0f,  0.0f, 0.0f,
                             0.0f,                         1.0f,  0.0f, 0.0f,
                             0.0f,                         0.0f,  1.0f, 0.0f,
            -off.x*(1.f/1024.f)*f,      off.y * (1.f/16384.f)*g,  0.0f, 1.0f
        };

    multiplyMatrix4f(transformMatrix, scaleMatrix);
    multiplyMatrix4f(transformMatrix, offsetMatrix);
    //POGOTODO: for later optimization purposes (batching/caching), I need to split the modelViewMatrix into modelMatrix and viewMatrix
    multiplyMatrix4f(transformMatrix, modelViewMatrix);

    //POGOTODO: I should instead implement one-sided sprites & culling by switching the xflip/yflip from flipping scale to instead flipping texScale
    //          Doing that will allow me to simplify a lot of this code, but it will require a lot of changes
    polymost2_drawVBO(GL_TRIANGLE_STRIP,
                      quadVertsID,
                      0,
                      4,
                      projectionMatrix,
                      transformMatrix,
                      method,
                      texScale,
                      texOffset,
                      ((globalorientation & CSTAT_SPRITE_ONE_SIDED) != 0)*3 &
                       ((((globalorientation & CSTAT_SPRITE_XFLIP) != 0) ^
                         ((globalorientation & CSTAT_SPRITE_YFLIP) != 0))+1));

    drawpoly_srepeat = 0;
    drawpoly_trepeat = 0;

    tilesiz[globalpicnum] = oldsiz;
}

void polymost_voxeditorfunc(int const spritenum, voxmodel_t *m, tspriteptr_t const tspr)
{
    if (searchit == 0)
        return;

    // Project 3D to 2D
    vec3f_t xyz = { (float)tspr->x, (float)tspr->y, (float)tspr->z };
    vec2_t off = { tspr->xoffset, tspr->yoffset };

    if (!(tspr->cstat&CSTAT_SPRITE_YCENTER))
    {
        if (tspr->cstat&CSTAT_SPRITE_YFLIP)
            xyz.z += m->piv.z*tspr->yrepeat*m->scale*(1.f/256.f);
        else
            xyz.z -= m->piv.z*tspr->yrepeat*m->scale*(1.f/256.f);
    }

    if (tspr->cstat&CSTAT_SPRITE_XFLIP) off.x = -off.x;
    if (tspr->cstat&CSTAT_SPRITE_YFLIP) off.y = -off.y;

    if ((tspr->cstat & CSTAT_SPRITE_ALIGNMENT) == CSTAT_SPRITE_ALIGNMENT_WALL)
    {
        const int32_t xv = tspr->xrepeat * sintable[(tspr->ang + 2560 + 1536) & 2047];
        const int32_t yv = tspr->xrepeat * sintable[(tspr->ang + 2048 + 1536) & 2047];


        xyz.x -= xv*off.x*(1.f/65536.f);
        xyz.y -= yv*off.x*(1.f/65536.f);
    }

    off.x = 0;

    vec2f_t s0 = { (float)(xyz.x - globalposx),
                    (float)(xyz.y - globalposy)};

    vec2f_t p0 = { s0.y * gcosang - s0.x * gsinang, s0.x * gcosang2 + s0.y * gsinang2 };

    if (p0.y <= SCISDIST)
        return;

    float const ryp0 = 1.f / p0.y;
    s0 = { ghalfx * p0.x * ryp0 + ghalfx, ((float)(xyz.z - globalposz)) * gyxscale * ryp0 + ghoriz };

    float const f = ryp0 * fxdimen * (1.0f / 160.f) * m->scale;

    vec2_t tsiz = { (m->siz.x+m->siz.y)>>1, m->siz.z };

    vec2f_t const ftsiz = { (float)tsiz.x, (float)tsiz.y };

    vec2f_t ff = { ((float)tspr->xrepeat) * f,
                    ((float)tspr->yrepeat) * f * ((float)yxaspect * (1.0f / 65536.f)) };

    if (tsiz.x & 1)
        s0.x += ff.x * 0.5f;
    if (globalorientation & 128 && tsiz.y & 1)
        s0.y += ff.y * 0.5f;

    s0.x -= ff.x * (float) off.x;
    s0.y -= ff.y * (float) off.y;

    ff.x *= ftsiz.x;
    ff.y *= ftsiz.y;

    vec2f_t pxy[4];

    pxy[0].x = pxy[3].x = s0.x - ff.x * 0.5f;
    pxy[1].x = pxy[2].x = s0.x + ff.x * 0.5f;

    if (!(globalorientation & 128))
    {
        pxy[0].y = pxy[1].y = s0.y - ff.y;
        pxy[2].y = pxy[3].y = s0.y;
    }
    else
    {
        pxy[0].y = pxy[1].y = s0.y - ff.y * 0.5f;
        pxy[2].y = pxy[3].y = s0.y + ff.y * 0.5f;
    }

#ifdef YAX_ENABLE
    if (yax_globallev == YAX_MAXDRAWS || searchit == 2)
#endif
    if (searchit >= 1)
    {
        otex.d = ryp0 * gviewxrange;
        xtex.d = ytex.d = 0;
        psectnum = tspr->sectnum;
        pwallnum = spritenum;
        psearchstat = 3;
        doeditorcheck = 1;
        polymost_polyeditorfunc(pxy, 4);
    }
}

void polymost_drawsprite(int32_t snum)
{
    if (r_enablepolymost2)
    {
        polymost2_drawsprite(snum);
        return;
    }

    auto const tspr = tspriteptr[snum];

    if (EDUKE32_PREDICT_FALSE(bad_tspr(tspr)))
        return;

    usectorptr_t sec;

    int32_t spritenum = tspr->owner;

    polymost_outputGLDebugMessage(3, "polymost_drawsprite(snum:%d)", snum);

    if ((tspr->cstat&48) != 48)
        tileUpdatePicnum(&tspr->picnum, spritenum + 32768);

    globalpicnum = tspr->picnum;
    globalshade = tspr->shade;
    globalpal = tspr->pal;
    globalorientation = tspr->cstat;
    globvis = globalvisibility;

    if (sector[tspr->sectnum].visibility != 0)
        globvis = mulscale4(globvis, (uint8_t)(sector[tspr->sectnum].visibility + 16));

    globvis2 = globalvisibility2;
    if (sector[tspr->sectnum].visibility != 0)
        globvis2 = mulscale4(globvis2, (uint8_t)(sector[tspr->sectnum].visibility + 16));
    polymost_setVisibility(globvis2);

    vec2_t off = { 0, 0 };

    if ((globalorientation & 48) != 48)  // only non-voxel sprites should do this
    {
        int const flag = usehightile && h_xsize[globalpicnum];
        off = { (int32_t)tspr->xoffset + (flag ? h_xoffs[globalpicnum] : picanm[globalpicnum].xofs),
                (int32_t)tspr->yoffset + (flag ? h_yoffs[globalpicnum] : picanm[globalpicnum].yofs) };
    }

    int32_t method = DAMETH_MASK | DAMETH_CLAMPED;

    if (tspr->cstat & 2)
        method = DAMETH_CLAMPED | ((tspr->cstat & 512) ? DAMETH_TRANS2 : DAMETH_TRANS1);

    handle_blend(!!(tspr->cstat & 2), tspr->blend, !!(tspr->cstat & 512));

    drawpoly_alpha = spriteext[spritenum].alpha;
    drawpoly_blend = tspr->blend;

    sec = (usectorptr_t)&sector[tspr->sectnum];

    if (!polymost_usetileshades() || (usehightile && hicfindsubst(globalpicnum, globalpal, hictinting[globalpal].f & HICTINT_ALWAYSUSEART))
        || (usemodels && md_tilehasmodel(globalpicnum, globalpal) >= 0))
        calc_and_apply_fog(fogshade(globalshade, globalpal), sec->visibility, get_floor_fogpal(sec));

    while (!(spriteext[spritenum].flags & SPREXT_NOTMD))
    {
        if (usemodels && tile2model[Ptile2tile(tspr->picnum, tspr->pal)].modelid >= 0 &&
            tile2model[Ptile2tile(tspr->picnum, tspr->pal)].framenum >= 0)
        {
            if (polymost_mddraw(tspr)) return;
            break;  // else, render as flat sprite
        }

        if (usevoxels && (tspr->cstat & 48) != 48 && tiletovox[tspr->picnum] >= 0 && voxmodels[tiletovox[tspr->picnum]])
        {
            if (polymost_voxdraw(voxmodels[tiletovox[tspr->picnum]], tspr))
            {
                if (editstatus)
                    polymost_voxeditorfunc(spritenum, voxmodels[tiletovox[tspr->picnum]], tspr);

                return;
            }
            break;  // else, render as flat sprite
        }

        if ((tspr->cstat & 48) == 48 && voxmodels[tspr->picnum])
        {
            polymost_voxdraw(voxmodels[tspr->picnum], tspr);

            if (editstatus)
                polymost_voxeditorfunc(spritenum, voxmodels[tspr->picnum], tspr);

            return;
        }

        break;
    }

    vec3_t pos = tspr->pos;

    if (spriteext[spritenum].flags & SPREXT_AWAY1)
    {
        pos.x += (sintable[(tspr->ang + 512) & 2047] >> 13);
        pos.y += (sintable[(tspr->ang) & 2047] >> 13);
    }
    else if (spriteext[spritenum].flags & SPREXT_AWAY2)
    {
        pos.x -= (sintable[(tspr->ang + 512) & 2047] >> 13);
        pos.y -= (sintable[(tspr->ang) & 2047] >> 13);
    }

    vec2_16_t const oldsiz = tilesiz[globalpicnum];
    vec2_t tsiz = { oldsiz.x, oldsiz.y };

    if (usehightile && h_xsize[globalpicnum])
        tsiz = { h_xsize[globalpicnum], h_ysize[globalpicnum] };

    if (tsiz.x <= 0 || tsiz.y <= 0)
        return;

    polymost_updaterotmat();

    vec2f_t const ftsiz = { (float) tsiz.x, (float) tsiz.y };

    switch ((globalorientation >> 4) & 3)
    {
        case 0:  // Face sprite
        {
            // Project 3D to 2D
            if (globalorientation & 4)
                off.x = -off.x;
            // NOTE: yoff not negated not for y flipping, unlike wall and floor
            // aligned sprites.

            int const ang = (getangle(tspr->x - globalposx, tspr->y - globalposy) + 1024) & 2047;

            float const foffs = TSPR_OFFSET(tspr);

            vec2f_t const offs = { (float) (sintable[(ang + 512) & 2047] >> 6) * foffs,
                (float) (sintable[(ang) & 2047] >> 6) * foffs };

            vec2f_t s0 = { (float)(tspr->x - globalposx) + offs.x,
                           (float)(tspr->y - globalposy) + offs.y};

            vec2f_t p0 = { s0.y * gcosang - s0.x * gsinang, s0.x * gcosang2 + s0.y * gsinang2 };

            if (p0.y <= SCISDIST)
                goto _drawsprite_return;

            float const ryp0 = 1.f / p0.y;
            s0 = { ghalfx * p0.x * ryp0 + ghalfx, ((float)(pos.z - globalposz)) * gyxscale * ryp0 + ghoriz };

            float const f = ryp0 * fxdimen * (1.0f / 160.f);

            vec2f_t ff = { ((float)tspr->xrepeat) * f,
                           ((float)tspr->yrepeat) * f * ((float)yxaspect * (1.0f / 65536.f)) };

            if (tsiz.x & 1)
                s0.x += ff.x * 0.5f;
            if (globalorientation & 128 && tsiz.y & 1)
                s0.y += ff.y * 0.5f;

            s0.x -= ff.x * (float) off.x;
            s0.y -= ff.y * (float) off.y;

            ff.x *= ftsiz.x;
            ff.y *= ftsiz.y;

            vec2f_t pxy[4];

            pxy[0].x = pxy[3].x = s0.x - ff.x * 0.5f;
            pxy[1].x = pxy[2].x = s0.x + ff.x * 0.5f;
            if (!(globalorientation & 128))
            {
                pxy[0].y = pxy[1].y = s0.y - ff.y;
                pxy[2].y = pxy[3].y = s0.y;
            }
            else
            {
                pxy[0].y = pxy[1].y = s0.y - ff.y * 0.5f;
                pxy[2].y = pxy[3].y = s0.y + ff.y * 0.5f;
            }

            xtex.d = ytex.d = ytex.u = xtex.v = 0;
            otex.d = ryp0 * gviewxrange;

            if (!(globalorientation & 4))
            {
                xtex.u = ftsiz.x * otex.d / (pxy[1].x - pxy[0].x + .002f);
                otex.u = -xtex.u * (pxy[0].x - .001f);
            }
            else
            {
                xtex.u = ftsiz.x * otex.d / (pxy[0].x - pxy[1].x - .002f);
                otex.u = -xtex.u * (pxy[1].x + .001f);
            }

            if (!(globalorientation & 8))
            {
                ytex.v = ftsiz.y * otex.d / (pxy[3].y - pxy[0].y + .002f);
                otex.v = -ytex.v * (pxy[0].y - .001f);
            }
            else
            {
                ytex.v = ftsiz.y * otex.d / (pxy[0].y - pxy[3].y - .002f);
                otex.v = -ytex.v * (pxy[3].y + .001f);
            }

            // sprite panning
            if (spriteext[spritenum].xpanning)
            {
                ytex.u -= ytex.d * ((float) (spriteext[spritenum].xpanning) * (1.0f / 255.f)) * ftsiz.x;
                otex.u -= otex.d * ((float) (spriteext[spritenum].xpanning) * (1.0f / 255.f)) * ftsiz.x;
                drawpoly_srepeat = 1;
            }

            if (spriteext[spritenum].ypanning)
            {
                ytex.v -= ytex.d * ((float) (spriteext[spritenum].ypanning) * (1.0f / 255.f)) * ftsiz.y;
                otex.v -= otex.d * ((float) (spriteext[spritenum].ypanning) * (1.0f / 255.f)) * ftsiz.y;
                drawpoly_trepeat = 1;
            }

            // Clip sprites to ceilings/floors when no parallaxing and not sloped
            if (!(sector[tspr->sectnum].ceilingstat & 3))
            {
                s0.y = ((float) (sector[tspr->sectnum].ceilingz - globalposz)) * gyxscale * ryp0 + ghoriz;
                if (pxy[0].y < s0.y)
                    pxy[0].y = pxy[1].y = s0.y;
            }

            if (!(sector[tspr->sectnum].floorstat & 3))
            {
                s0.y = ((float) (sector[tspr->sectnum].floorz - globalposz)) * gyxscale * ryp0 + ghoriz;
                if (pxy[2].y > s0.y)
                    pxy[2].y = pxy[3].y = s0.y;
            }

            tilesiz[globalpicnum] = { (int16_t)tsiz.x, (int16_t)tsiz.y };
            pow2xsplit = 0;
#ifdef YAX_ENABLE
            if (yax_globallev == YAX_MAXDRAWS || searchit == 2)
#endif
            if (searchit >= 1)
            {
                psectnum = tspr->sectnum;
                pwallnum = spritenum;
                psearchstat = 3;
                doeditorcheck = 1;
            }
            polymost_drawpoly(pxy, 4, method);
            doeditorcheck = 0;

            drawpoly_srepeat = 0;
            drawpoly_trepeat = 0;
        }
        break;

        case 1:  // Wall sprite
        {
            // Project 3D to 2D
            if (globalorientation & 4)
                off.x = -off.x;

            if (globalorientation & 8)
                off.y = -off.y;

            vec2f_t const extent = { (float)tspr->xrepeat * (float)sintable[(tspr->ang) & 2047] * (1.0f / 65536.f),
                                     (float)tspr->xrepeat * (float)sintable[(tspr->ang + 1536) & 2047] * (1.0f / 65536.f) };

            float f = (float)(tsiz.x >> 1) + (float)off.x;

            vec2f_t const vf = { extent.x * f, extent.y * f };

            vec2f_t vec0 = { (float)(pos.x - globalposx) - vf.x,
                             (float)(pos.y - globalposy) - vf.y };

            int32_t const s = tspr->owner;
            int32_t walldist = 1;
            int32_t w = (s == -1) ? -1 : wsprinfo[s].wall;

            // find the wall most likely to be what the sprite is supposed to be ornamented against
            // this is really slow, so cache the result
            if (s == -1 || !wsprinfo[s].wall || (spritechanged[s] != wsprinfo[s].srev) ||
                (w != -1 && wallchanged[w] != wsprinfo[s].wrev))
            {
                w = polymost_findwall(tspr, &tsiz, &walldist);

                if (s != -1)
                {
                    wallspriteinfo_t *ws = &wsprinfo[s];
                    ws->wall = w;

                    if (w != -1)
                    {
                        ws->wdist = walldist;
                        ws->wrev = wallchanged[w];
                        ws->srev = spritechanged[s];
                    }
                }
            }
            else if (s != -1)
                walldist = wsprinfo[s].wdist;

            // detect if the sprite is either on the wall line or the wall line and sprite intersect
            if (w != -1)
            {
                vec2_t v = { /*Blrintf(vf.x)*/(int)vf.x, /*Blrintf(vf.y)*/(int)vf.y };

                if (walldist <= 2 || ((pos.x - v.x) + (pos.x + v.x)) == (wall[w].x + POINT2(w).x) ||
                    ((pos.y - v.y) + (pos.y + v.y)) == (wall[w].y + POINT2(w).y) ||
                    polymost_lintersect(pos.x - v.x, pos.y - v.y, pos.x + v.x, pos.y + v.y, wall[w].x, wall[w].y,
                                        POINT2(w).x, POINT2(w).y))
                {
                    int32_t const ang = getangle(wall[w].x - POINT2(w).x, wall[w].y - POINT2(w).y);
                    float const foffs = TSPR_OFFSET(tspr);
                    vec2f_t const offs = { (float)(sintable[(ang + 1024) & 2047] >> 6) * foffs,
                                     (float)(sintable[(ang + 512) & 2047] >> 6) * foffs};

                    vec0.x -= offs.x;
                    vec0.y -= offs.y;
                }
            }

            vec2f_t p0 = { vec0.y * gcosang - vec0.x * gsinang,
                           vec0.x * gcosang2 + vec0.y * gsinang2 };

            vec2f_t const pp = { extent.x * ftsiz.x + vec0.x,
                                 extent.y * ftsiz.x + vec0.y };

            vec2f_t p1 = { pp.y * gcosang - pp.x * gsinang,
                           pp.x * gcosang2 + pp.y * gsinang2 };

            if ((p0.y <= SCISDIST) && (p1.y <= SCISDIST))
                goto _drawsprite_return;

            // Clip to close parallel-screen plane
            vec2f_t const op0 = p0;

            float t0 = 0.f, t1 = 1.f;

            if (p0.y < SCISDIST)
            {
                t0 = (SCISDIST - p0.y) / (p1.y - p0.y);
                p0 = { (p1.x - p0.x) * t0 + p0.x, SCISDIST };
            }

            if (p1.y < SCISDIST)
            {
                t1 = (SCISDIST - op0.y) / (p1.y - op0.y);
                p1 = { (p1.x - op0.x) * t1 + op0.x, SCISDIST };
            }

            f = 1.f / p0.y;
            const float ryp0 = f * gyxscale;
            float sx0 = ghalfx * p0.x * f + ghalfx;

            f = 1.f / p1.y;
            const float ryp1 = f * gyxscale;
            float sx1 = ghalfx * p1.x * f + ghalfx;

            pos.z -= ((off.y * tspr->yrepeat) << 2);

            if (globalorientation & 128)
            {
                pos.z += ((tsiz.y * tspr->yrepeat) << 1);

                if (tsiz.y & 1)
                    pos.z += (tspr->yrepeat << 1);  // Odd yspans
            }

            xtex.d = (ryp0 - ryp1) * gxyaspect / (sx0 - sx1);
            ytex.d = 0;
            otex.d = ryp0 * gxyaspect - xtex.d * sx0;

            if (globalorientation & 4)
            {
                t0 = 1.f - t0;
                t1 = 1.f - t1;
            }

            // sprite panning
            if (spriteext[spritenum].xpanning)
            {
                float const xpan = ((float)(spriteext[spritenum].xpanning) * (1.0f / 255.f));
                t0 -= xpan;
                t1 -= xpan;
                drawpoly_srepeat = 1;
            }

            xtex.u = (t0 * ryp0 - t1 * ryp1) * gxyaspect * ftsiz.x / (sx0 - sx1);
            ytex.u = 0;
            otex.u = t0 * ryp0 * gxyaspect * ftsiz.x - xtex.u * sx0;

            f = ((float) tspr->yrepeat) * ftsiz.y * 4;

            float sc0 = ((float) (pos.z - globalposz - f)) * ryp0 + ghoriz;
            float sc1 = ((float) (pos.z - globalposz - f)) * ryp1 + ghoriz;
            float sf0 = ((float) (pos.z - globalposz)) * ryp0 + ghoriz;
            float sf1 = ((float) (pos.z - globalposz)) * ryp1 + ghoriz;

            // gvx*sx0 + gvy*sc0 + gvo = 0
            // gvx*sx1 + gvy*sc1 + gvo = 0
            // gvx*sx0 + gvy*sf0 + gvo = tsizy*(gdx*sx0 + gdo)
            f = ftsiz.y * (xtex.d * sx0 + otex.d) / ((sx0 - sx1) * (sc0 - sf0));

            if (!(globalorientation & 8))
            {
                xtex.v = (sc0 - sc1) * f;
                ytex.v = (sx1 - sx0) * f;
                otex.v = -xtex.v * sx0 - ytex.v * sc0;
            }
            else
            {
                xtex.v = (sf1 - sf0) * f;
                ytex.v = (sx0 - sx1) * f;
                otex.v = -xtex.v * sx0 - ytex.v * sf0;
            }

            // sprite panning
            if (spriteext[spritenum].ypanning)
            {
                float const ypan = ((float)(spriteext[spritenum].ypanning) * (1.0f / 255.f)) * ftsiz.y;
                xtex.v -= xtex.d * ypan;
                ytex.v -= ytex.d * ypan;
                otex.v -= otex.d * ypan;
                drawpoly_trepeat = 1;
            }

            // Clip sprites to ceilings/floors when no parallaxing
            if (!(sector[tspr->sectnum].ceilingstat & 1))
            {
                if (sector[tspr->sectnum].ceilingz > pos.z - (float)((tspr->yrepeat * tsiz.y) << 2))
                {
                    sc0 = (float)(sector[tspr->sectnum].ceilingz - globalposz) * ryp0 + ghoriz;
                    sc1 = (float)(sector[tspr->sectnum].ceilingz - globalposz) * ryp1 + ghoriz;
                }
            }
            if (!(sector[tspr->sectnum].floorstat & 1))
            {
                if (sector[tspr->sectnum].floorz < pos.z)
                {
                    sf0 = (float)(sector[tspr->sectnum].floorz - globalposz) * ryp0 + ghoriz;
                    sf1 = (float)(sector[tspr->sectnum].floorz - globalposz) * ryp1 + ghoriz;
                }
            }

            if (sx0 > sx1)
            {
                if (globalorientation & 64)
                    goto _drawsprite_return;  // 1-sided sprite

                swapfloat(&sx0, &sx1);
                swapfloat(&sc0, &sc1);
                swapfloat(&sf0, &sf1);
            }

            vec2f_t const pxy[4] = { { sx0, sc0 }, { sx1, sc1 }, { sx1, sf1 }, { sx0, sf0 } };

            tilesiz[globalpicnum] = { (int16_t)tsiz.x, (int16_t)tsiz.y };
            pow2xsplit = 0;
#ifdef YAX_ENABLE
            if (yax_globallev == YAX_MAXDRAWS || searchit == 2)
#endif
            if (searchit >= 1)
            {
                psectnum = tspr->sectnum;
                pwallnum = spritenum;
                psearchstat = 3;
                doeditorcheck = 1;
            }
            polymost_drawpoly(pxy, 4, method);
            doeditorcheck = 0;

            drawpoly_srepeat = 0;
            drawpoly_trepeat = 0;
        }
        break;

        case 2:  // Floor sprite
            globvis2 = globalhisibility2;
            if (sector[tspr->sectnum].visibility != 0)
                globvis2 = mulscale4(globvis2, (uint8_t)(sector[tspr->sectnum].visibility + 16));
            polymost_setVisibility(globvis2);

            if ((globalorientation & 64) != 0 && (globalposz > pos.z) == (!(globalorientation & 8)))
                goto _drawsprite_return;
            else
            {
                if ((globalorientation & 4) > 0)
                    off.x = -off.x;
                if ((globalorientation & 8) > 0)
                    off.y = -off.y;

                vec2f_t const p0 = { (float)(((tsiz.x + 1) >> 1) - off.x) * tspr->xrepeat,
                                     (float)(((tsiz.y + 1) >> 1) - off.y) * tspr->yrepeat },
                              p1 = { (float)((tsiz.x >> 1) + off.x) * tspr->xrepeat,
                                     (float)((tsiz.y >> 1) + off.y) * tspr->yrepeat };

                float const c = sintable[(tspr->ang + 512) & 2047] * (1.0f / 65536.f);
                float const s = sintable[tspr->ang & 2047] * (1.0f / 65536.f);

                vec2f_t pxy[6];

                // Project 3D to 2D
                for (bssize_t j = 0; j < 4; j++)
                {
                    vec2f_t s0 = { (float)(tspr->x - globalposx), (float)(tspr->y - globalposy) };

                    if ((j + 0) & 2)
                    {
                        s0.y -= s * p0.y;
                        s0.x -= c * p0.y;
                    }
                    else
                    {
                        s0.y += s * p1.y;
                        s0.x += c * p1.y;
                    }
                    if ((j + 1) & 2)
                    {
                        s0.x -= s * p0.x;
                        s0.y += c * p0.x;
                    }
                    else
                    {
                        s0.x += s * p1.x;
                        s0.y -= c * p1.x;
                    }

                    pxy[j] = { s0.y * gcosang - s0.x * gsinang, s0.x * gcosang2 + s0.y * gsinang2 };
                }

                if (pos.z < globalposz)  // if floor sprite is above you, reverse order of points
                {
                    EDUKE32_STATIC_ASSERT(sizeof(uint64_t) == sizeof(vec2f_t));

                    swap64bit(&pxy[0], &pxy[1]);
                    swap64bit(&pxy[2], &pxy[3]);
                }

                // Clip to SCISDIST plane
                int32_t npoints = 0;
                vec2f_t p2[6];

                for (bssize_t i = 0, j = 1; i < 4; j = ((++i + 1) & 3))
                {
                    if (pxy[i].y >= SCISDIST)
                        p2[npoints++] = pxy[i];

                    if ((pxy[i].y >= SCISDIST) != (pxy[j].y >= SCISDIST))
                    {
                        float const f = (SCISDIST - pxy[i].y) / (pxy[j].y - pxy[i].y);
                        vec2f_t const t = { (pxy[j].x - pxy[i].x) * f + pxy[i].x,
                                            (pxy[j].y - pxy[i].y) * f + pxy[i].y };
                        p2[npoints++] = t;
                    }
                }

                if (npoints < 3)
                    goto _drawsprite_return;

                // Project rotated 3D points to screen

                int fadjust = 0;

                // unfortunately, offsetting by only 1 isn't enough on most Android devices
                if (pos.z == sec->ceilingz || pos.z == sec->ceilingz + 1)
                    pos.z = sec->ceilingz + 2, fadjust = (tspr->owner & 31);

                if (pos.z == sec->floorz || pos.z == sec->floorz - 1)
                    pos.z = sec->floorz - 2, fadjust = -((tspr->owner & 31));

                float f = (float)(pos.z - globalposz + fadjust) * gyxscale;

                for (bssize_t j = 0; j < npoints; j++)
                {
                    float const ryp0 = 1.f / p2[j].y;
                    pxy[j] = { ghalfx * p2[j].x * ryp0 + ghalfx, f * ryp0 + ghoriz };
                }

                // gd? Copied from floor rendering code

                xtex.d = 0;
                ytex.d = gxyaspect / (double)(pos.z - globalposz + fadjust);
                otex.d = -ghoriz * ytex.d;

                // copied&modified from relative alignment
                vec2f_t const vv = { (float)tspr->x + s * p1.x + c * p1.y, (float)tspr->y + s * p1.y - c * p1.x };
                vec2f_t ff = { -(p0.x + p1.x) * s, (p0.x + p1.x) * c };

                f = polymost_invsqrt_approximation(ff.x * ff.x + ff.y * ff.y);

                ff.x *= f;
                ff.y *= f;

                float const ft[4] = { ((float)(globalposy - vv.y)) * ff.y + ((float)(globalposx - vv.x)) * ff.x,
                                      ((float)(globalposx - vv.x)) * ff.y - ((float)(globalposy - vv.y)) * ff.x,
                                      fsinglobalang * ff.y + fcosglobalang * ff.x,
                                      fsinglobalang * ff.x - fcosglobalang * ff.y };

                f = fviewingrange * -(1.f / (65536.f * 262144.f));
                xtex.u = (float)ft[3] * f;
                xtex.v = (float)ft[2] * f;
                ytex.u = ft[0] * ytex.d;
                ytex.v = ft[1] * ytex.d;
                otex.u = ft[0] * otex.d;
                otex.v = ft[1] * otex.d;
                otex.u += (ft[2] * (1.0f / 262144.f) - xtex.u) * ghalfx;
                otex.v -= (ft[3] * (1.0f / 262144.f) + xtex.v) * ghalfx;

                f = 4.f / (float)tspr->xrepeat;
                xtex.u *= f;
                ytex.u *= f;
                otex.u *= f;

                f = -4.f / (float)tspr->yrepeat;
                xtex.v *= f;
                ytex.v *= f;
                otex.v *= f;

                if (globalorientation & 4)
                {
                    xtex.u = ftsiz.x * xtex.d - xtex.u;
                    ytex.u = ftsiz.x * ytex.d - ytex.u;
                    otex.u = ftsiz.x * otex.d - otex.u;
                }

                // sprite panning
                if (spriteext[spritenum].xpanning)
                {
                    float const f = ((float)(spriteext[spritenum].xpanning) * (1.0f / 255.f)) * ftsiz.x;
                    ytex.u -= ytex.d * f;
                    otex.u -= otex.d * f;
                    drawpoly_srepeat = 1;
                }

                if (spriteext[spritenum].ypanning)
                {
                    float const f = ((float)(spriteext[spritenum].ypanning) * (1.0f / 255.f)) * ftsiz.y;
                    ytex.v -= ytex.d * f;
                    otex.v -= otex.d * f;
                    drawpoly_trepeat = 1;
                }
                
                tilesiz[globalpicnum] = { (int16_t)tsiz.x, (int16_t)tsiz.y };
                pow2xsplit = 0;

#ifdef YAX_ENABLE
                if (yax_globallev == YAX_MAXDRAWS || searchit == 2)
#endif
                if (searchit >= 1)
                {
                    psectnum = tspr->sectnum;
                    pwallnum = spritenum;
                    psearchstat = 3;
                    doeditorcheck = 1;
                }
                polymost_drawpoly(pxy, npoints, method);
                doeditorcheck = 0;

                drawpoly_srepeat = 0;
                drawpoly_trepeat = 0;
            }

            break;

        case 3:  // Voxel sprite
            break;
    }

    if (automapping == 1 && (unsigned)spritenum < MAXSPRITES)
        show2dsprite[spritenum>>3] |= pow2char[spritenum&7];

_drawsprite_return:
    polymost_identityrotmat();
    tilesiz[globalpicnum] = oldsiz;
}

EDUKE32_STATIC_ASSERT((int)RS_YFLIP == (int)HUDFLAG_FLIPPED);

//sx,sy       center of sprite; screen coords*65536
//z           zoom*65536. > is zoomed in
//a           angle (0 is default)
//dastat&1    1:translucence
//dastat&2    1:auto-scale mode (use 320*200 coordinates)
//dastat&4    1:y-flip
//dastat&8    1:don't clip to startumost/startdmost
//dastat&16   1:force point passed to be top-left corner, 0:Editart center
//dastat&32   1:reverse translucence
//dastat&64   1:non-masked, 0:masked
//dastat&128  1:draw all pages (permanent)
//cx1,...     clip window (actual screen coords)

void polymost_dorotatespritemodel(int32_t sx, int32_t sy, int32_t z, int16_t a, int16_t picnum,
    int8_t dashade, char dapalnum, int32_t dastat, uint8_t daalpha, uint8_t dablend, int32_t uniqid)
{
    float d, cosang, sinang, cosang2, sinang2;
    float m[4][4];

    const int32_t tilenum = Ptile2tile(picnum, dapalnum);

    if (tile2model[tilenum].modelid == -1 || tile2model[tilenum].framenum == -1)
        return;

    vec3f_t vec1;

    tspritetype tspr{};

    hudtyp const * const hud = tile2model[tilenum].hudmem[(dastat&4)>>2];

    if (!hud || hud->flags & HUDFLAG_HIDE)
        return;

    polymost_outputGLDebugMessage(3, "polymost_dorotatespritemodel(sx:%d, sy:%d, z:%d, a:%hd, picnum:%hd, dashade:%hhd, dapalnum:%hhu, dastat:%d, daalpha:%hhu, dablend:%hhu, uniqid:%d)",
                                  sx, sy, z, a, picnum, dashade, dapalnum, dastat, daalpha, dablend, uniqid);

    float const ogchang = gchang; gchang = 1.f;
    float const ogshang = gshang; gshang = 0.f; d = (float) z*(1.0f/(65536.f*16384.f));
    float const ogctang = gctang; gctang = (float) sintable[(a+512)&2047]*d;
    float const ogstang = gstang; gstang = (float) sintable[a&2047]*d;
    int const ogshade  = globalshade;  globalshade  = dashade;
    int const ogpal    = globalpal;    globalpal    = (int32_t) ((uint8_t) dapalnum);
    double const ogxyaspect = gxyaspect; gxyaspect = 1.f;
    int const oldviewingrange = viewingrange; viewingrange = 65536;
    float const oldfviewingrange = fviewingrange; fviewingrange = 65536.f;
    float const ogvrcorrection = gvrcorrection; gvrcorrection = 1.f;

    vec1 = hud->add;

#ifdef POLYMER
    if (pr_overridehud) {
        vec1.x = pr_hudxadd;
        vec1.y = pr_hudyadd;
        vec1.z = pr_hudzadd;
    }
#endif
    if (!(hud->flags & HUDFLAG_NOBOB))
    {
        vec2f_t f = { (float)sx * (1.f / 65536.f), (float)sy * (1.f / 65536.f) };

        if (dastat & RS_TOPLEFT)
        {
            vec2_16_t siz = tilesiz[picnum];
            vec2_16_t off = { (int16_t)((siz.x >> 1) + picanm[picnum].xofs), (int16_t)((siz.y >> 1) + picanm[picnum].yofs) };

            d = (float)z * (1.0f / (65536.f * 16384.f));
            cosang2 = cosang = (float)sintable[(a + 512) & 2047] * d;
            sinang2 = sinang = (float)sintable[a & 2047] * d;

            if ((dastat & RS_AUTO) || (!(dastat & RS_NOCLIP)))  // Don't aspect unscaled perms
            {
                d = (float)xyaspect * (1.0f / 65536.f);
                cosang2 *= d;
                sinang2 *= d;
            }

            vec2f_t const foff = { (float)off.x, (float)off.y };
            f.x += -foff.x * cosang2 + foff.y * sinang2;
            f.y += -foff.x * sinang  - foff.y * cosang;
        }

        if (!(dastat & RS_AUTO))
        {
            vec1.x += f.x / ((float)(xdim << 15)) - 1.f;  //-1: left of screen, +1: right of screen
            vec1.y += f.y / ((float)(ydim << 15)) - 1.f;  //-1: top of screen, +1: bottom of screen
        }
        else
        {
            vec1.x += f.x * (1.0f / 160.f) - 1.f;  //-1: left of screen, +1: right of screen
            vec1.y += f.y * (1.0f / 100.f) - 1.f;  //-1: top of screen, +1: bottom of screen
        }
    }
    tspr.ang = hud->angadd+globalang;

#ifdef POLYMER
    if (pr_overridehud) {
        tspr.ang = pr_hudangadd + globalang;
    }
#endif

    if (dastat & RS_YFLIP) { vec1.x = -vec1.x; vec1.y = -vec1.y; }

    // In Polymost, we don't care if the model is very big
#ifdef POLYMER
    if (videoGetRenderMode() == REND_POLYMER)
    {
        vec3f_t const vec2 = { fglobalposx + (gcosang * vec1.z - gsinang * vec1.x) * 2560.f,
                               fglobalposy + (gsinang * vec1.z + gcosang * vec1.x) * 2560.f,
                               fglobalposz + (vec1.y * (2560.f * 0.8f)) };
        *(vec3f_t *)&tspr = vec2;
        tspr.xrepeat = tspr.yrepeat = 5;
    }
    else
#endif
    {
        tspr.xrepeat = tspr.yrepeat = 32;

        tspr.x = globalposx + Blrintf((gcosang*vec1.z - gsinang*vec1.x)*16384.f);
        tspr.y = globalposy + Blrintf((gsinang*vec1.z + gcosang*vec1.x)*16384.f);
        tspr.z = globalposz + Blrintf(vec1.y * (16384.f * 0.8f));
    }

    tspr.picnum = picnum;
    tspr.shade = dashade;
    tspr.pal = dapalnum;
    tspr.owner = uniqid+MAXSPRITES;
    // 1 -> 1
    // 32 -> 32*16 = 512
    // 4 -> 8
    tspr.cstat = globalorientation = (dastat&RS_TRANS1) | ((dastat&RS_TRANS2)<<4) | ((dastat&RS_YFLIP)<<1);

    if ((dastat&(RS_AUTO|RS_NOCLIP)) == RS_AUTO)
        glViewport(windowxy1.x, ydim-(windowxy2.y+1), windowxy2.x-windowxy1.x+1, windowxy2.y-windowxy1.y+1);
    else
        glViewport(0, 0, xdim, ydim);

    if (videoGetRenderMode() < REND_POLYMER)
    {
        glMatrixMode(GL_PROJECTION);
        Bmemset(m, 0, sizeof(m));

        if ((dastat&(RS_AUTO|RS_NOCLIP)) == RS_AUTO)
        {
            float f = 1.f;
            int32_t fov = hud->fov;
#ifdef POLYMER
            if (pr_overridehud)
                fov = pr_hudfov;
#endif
            if (fov != -1)
                f = 1.f/tanf(((float)fov * 2.56f) * ((.5f * fPI) * (1.0f/2048.f)));

            m[0][0] = f*fydimen; m[0][2] = 1.f;
            m[1][1] = f*fxdimen; m[1][2] = 1.f;
            m[2][2] = 1.f; m[2][3] = fydimen;
            m[3][2] =-1.f;
        }
        else
        {
            m[0][0] = m[2][3] = 1.f;
            m[1][1] = fxdim/fydim;
            m[2][2] = 1.0001f;
            m[3][2] = 1-m[2][2];
        }

        glLoadMatrixf(&m[0][0]);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    if (hud->flags & HUDFLAG_NODEPTH)
        glDisable(GL_DEPTH_TEST);
    else
    {
        static int32_t onumframes = 0;

        glEnable(GL_DEPTH_TEST);

        if (onumframes != numframes)
        {
            onumframes = numframes;
            glClear(GL_DEPTH_BUFFER_BIT);
        }
    }

    spriteext[tspr.owner].alpha = daalpha * (1.0f / 255.0f);
    tspr.blend = dablend;

    polymost_setFogEnabled(false);

    if (videoGetRenderMode() == REND_POLYMOST)
        polymost_mddraw(&tspr);
# ifdef POLYMER
    else
    {
        int32_t fov;

        tspriteptr[maxspritesonscreen] = &tspr;

        glEnable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);

        spriteext[tspr.owner].roll = a;
        spriteext[tspr.owner].pivot_offset.z = z;

        fov = hud->fov;

        if (fov == -1)
            fov = pr_fov;

        if (pr_overridehud)
            fov = pr_hudfov;

        polymer_setaspect(fov);

        polymer_drawsprite(maxspritesonscreen);

        polymer_setaspect(pr_fov);

        spriteext[tspr.owner].pivot_offset.z = 0;
        spriteext[tspr.owner].roll = 0;

        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
    }
# endif
    if (!nofog) polymost_setFogEnabled(true);

    gvrcorrection = ogvrcorrection;
    viewingrange = oldviewingrange;
    fviewingrange = oldfviewingrange;
    gxyaspect = ogxyaspect;
    globalshade  = ogshade;
    globalpal    = ogpal;
    gchang = ogchang;
    gshang = ogshang;
    gctang = ogctang;
    gstang = ogstang;
}

void polymost_dorotatesprite(int32_t sx, int32_t sy, int32_t z, int16_t a, int16_t picnum,
                             int8_t dashade, char dapalnum, int32_t dastat, uint8_t daalpha, uint8_t dablend,
                             int32_t cx1, int32_t cy1, int32_t cx2, int32_t cy2, int32_t uniqid)
{
    if (usemodels && tile2model[picnum].hudmem[(dastat&4)>>2])
    {
        polymost_dorotatespritemodel(sx, sy, z, a, picnum, dashade, dapalnum, dastat, daalpha, dablend, uniqid);
        return;
    }

    polymost_outputGLDebugMessage(3, "polymost_dorotatesprite(sx:%d, sy:%d, z:%d, a:%hd, picnum:%hd, dashade:%hhd, dapalnum:%hhu, dastat:%d, daalpha:%hhu, dablend:%hhu, cx1:%d, cy1:%d, cx2:%d, cy2:%d, uniqid:%d)",
                                  sx, sy, z, a, picnum, dashade, dapalnum, dastat, daalpha, dablend, cx1, cy1, cx2, cy2, uniqid);

    glViewport(0,0,xdim,ydim);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();

    globvis = 0;
    globvis2 = 0;
    polymost_setClamp(1+2);
    polymost_setVisibility(globvis2);

    int32_t const ogpicnum = globalpicnum;
    globalpicnum = picnum;
    int32_t const  ogshade = globalshade;
    globalshade = dashade;
    int32_t const  ogpal = globalpal;
    globalpal = (int32_t)((uint8_t)dapalnum);
    float const  oghalfx = ghalfx;
    ghalfx = fxdim * .5f;
    float const  oghalfy = ghalfy;
    ghalfy = fydim * .5f;
    float const  ogrhalfxdown10 = grhalfxdown10;
    grhalfxdown10 = 1.f / (ghalfx * 1024.f);
    float const  ogrhalfxdown10x = grhalfxdown10x;
    grhalfxdown10x = grhalfxdown10;
    float const  oghoriz = ghoriz;
    ghoriz = fydim * .5f;
    int32_t const  ofoffset = frameoffset;
    frameoffset = frameplace;
    float const  ogchang = gchang;
    gchang = 1.f;
    float const  ogshang = gshang;
    gshang = 0.f;
    float const  ogctang = gctang;
    gctang = 1.f;
    float const  ogstang = gstang;
    gstang = 0.f;
    float const  ogvrcorrection = gvrcorrection;
    gvrcorrection = 1.f;

    polymost_updaterotmat();

    float m[4][4];

    Bmemset(m,0,sizeof(m));
    m[0][0] = m[2][3] = 1.0f;
    m[1][1] = fxdim / fydim;
    m[2][2] = 1.0001f;
    m[3][2] = 1 - m[2][2];

    glLoadMatrixf(&m[0][0]);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

#if defined(POLYMER)
# ifdef USE_GLEXT
    const int32_t olddetailmapping = r_detailmapping, oldglowmapping = r_glowmapping;
# endif
    const int32_t oldnormalmapping = pr_normalmapping;
#endif

    int32_t method = DAMETH_CLAMPED; //Use OpenGL clamping - dorotatesprite never repeats

    if (!(dastat & RS_NOMASK))
    {
        glEnable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);

        if (dastat & RS_TRANS1)
            method |= (dastat & RS_TRANS2) ? DAMETH_TRANS2 : DAMETH_TRANS1;
        else
            method |= DAMETH_MASK;
    }
    else
    {
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
    }

    handle_blend(!!(dastat & RS_TRANS1), dablend, !!(dastat & RS_TRANS2));

#ifdef POLYMER
    if (videoGetRenderMode() == REND_POLYMER)
    {
        pr_normalmapping = 0;
        polymer_inb4rotatesprite(picnum, dapalnum, dashade, method);
        polymost_resetVertexPointers();
# ifdef USE_GLEXT
        r_detailmapping = 0;
        r_glowmapping = 0;
# endif
    }
#endif

    drawpoly_alpha = daalpha * (1.0f / 255.0f);
    drawpoly_blend = dablend;

    vec2_16_t const siz = tilesiz[globalpicnum];
    vec2_16_t ofs = { 0, 0 };

    if (!(dastat & RS_TOPLEFT))
    {
        ofs = { int16_t(picanm[globalpicnum].xofs + (siz.x>>1)),
                int16_t(picanm[globalpicnum].yofs + (siz.y>>1)) };
    }

    if (dastat & RS_YFLIP)
        ofs.y = siz.y - ofs.y;

    int32_t ourxyaspect, ouryxaspect;
    dorotspr_handle_bit2(&sx, &sy, &z, dastat, cx1 + cx2, cy1 + cy2, &ouryxaspect, &ourxyaspect);

    int32_t cosang = mulscale14(sintable[(a + 512) & 2047], z);
    int32_t cosang2 = cosang;
    int32_t sinang = mulscale14(sintable[a & 2047], z);
    int32_t sinang2 = sinang;

    if ((dastat & RS_AUTO) || (!(dastat & RS_NOCLIP)))  // Don't aspect unscaled perms
    {
        cosang2 = mulscale16(cosang2,ourxyaspect);
        sinang2 = mulscale16(sinang2,ourxyaspect);
    }

    int32_t const cx = sx - ofs.x * cosang2 + ofs.y * sinang2;
    int32_t const cy = sy - ofs.x * sinang  - ofs.y * cosang;

    vec2_t pxy[8] = { { cx, cy },
                      { cx + siz.x * cosang2, cy + siz.x * sinang },
                      { 0, 0 },
                      { cx - siz.y * sinang2, cy + siz.y * cosang } };

    pxy[2]= { pxy[1].x + pxy[3].x - pxy[0].x,
              pxy[1].y + pxy[3].y - pxy[0].y };

    vec2_t const gxy = pxy[0];

    //Clippoly4

    int32_t n = 4, nn = 0, nz = 0;
    int32_t px2[8], py2[8];

    cx2++;
    cy2++;

    cx1 <<= 16;
    cy1 <<= 16;
    cx2 <<= 16;
    cy2 <<= 16;

    do
    {
        int32_t zz = nz+1; if (zz == n) zz = 0;
        int32_t const x1 = pxy[nz].x, x2 = pxy[zz].x-x1;
        if ((cx1 <= x1) && (x1 <= cx2)) { px2[nn] = x1; py2[nn] = pxy[nz].y; nn++; }
        int32_t fx = (x2 <= 0 ? cx2 : cx1), t = fx-x1;
        if ((t < x2) != (t < 0)) { px2[nn] = fx; py2[nn] = scale(pxy[zz].y-pxy[nz].y,t,x2) + pxy[nz].y; nn++; }
        fx = (x2 <= 0 ? cx1 : cx2); t = fx-x1;
        if ((t < x2) != (t < 0)) { px2[nn] = fx; py2[nn] = scale(pxy[zz].y-pxy[nz].y,t,x2) + pxy[nz].y; nn++; }
        nz = zz;
    }
    while (nz);

    n = 0;

    if (nn >= 3)
    {
        nz = 0;
        do
        {
            int32_t zz = nz+1; if (zz == nn) zz = 0;
            int32_t const y1 = py2[nz], y2 = py2[zz]-y1;
            if ((cy1 <= y1) && (y1 <= cy2)) { pxy[n].y = y1; pxy[n].x = px2[nz]; n++; }
            int32_t fy = (y2 <= 0 ? cy2 : cy1), t = fy - y1;
            if ((t < y2) != (t < 0)) { pxy[n].y = fy; pxy[n].x = scale(px2[zz]-px2[nz],t,y2) + px2[nz]; n++; }
            fy = (y2 <= 0 ? cy1 : cy2); t = fy - y1;
            if ((t < y2) != (t < 0)) { pxy[n].y = fy; pxy[n].x = scale(px2[zz]-px2[nz],t,y2) + px2[nz]; n++; }
            nz = zz;
        }
        while (nz);
    }

    if (n >= 3)
    {
        int32_t i = divscale32(1,z);
        int32_t xv = mulscale14(sintable[a&2047],i);
        int32_t yv = mulscale14(sintable[(a+512)&2047],i);
        int32_t xv2, yv2;
        if ((dastat&RS_AUTO) || (dastat&RS_NOCLIP)==0) //Don't aspect unscaled perms
        {
            yv2 = mulscale16(-xv,ouryxaspect);
            xv2 = mulscale16(yv,ouryxaspect);
        }
        else
        {
            yv2 = -xv;
            xv2 = yv;
        }

        int32_t lx = pxy[0].x;
        for (int v=n-1; v>0; v--)
            if (pxy[v].x < lx) lx = pxy[v].x;

        vec2_t oxy = { (lx>>16), 0 };
        int32_t x = (oxy.x<<16)-1-gxy.x;
        int32_t y = (oxy.y<<16)+65535-gxy.y;
        int32_t bx = dmulscale16(x,xv2,y,xv);
        int32_t by = dmulscale16(x,yv2,y,yv);

        if (dastat & RS_YFLIP)
        {
            yv = -yv;
            yv2 = -yv2;
            by = (siz.y<<16)-1-by;
        }

        vec2f_t fpxy[8];
        for (int v=0; v<n; v++)
            fpxy[v] = { float((pxy[v].x+8192)>>16), float((pxy[v].y+8192)>>16) };

        xtex.d = 0; ytex.d = 0; otex.d = 1.0;
        otex.u = (bx-(oxy.x-1+0.7)*xv2-(oxy.y+0.7)*xv)*(1.0/65536.0);
        xtex.u = xv2*(1.0/65536.0);
        ytex.u = xv*(1.0/65536.0);
        otex.v = (by-(oxy.x-1+0.7)*yv2-(oxy.y+0.7)*yv)*(1.0/65536.0);
        xtex.v = yv2*(1.0/65536.0);
        ytex.v = yv*(1.0/65536.0);

        polymost_setFogEnabled(false);
        pow2xsplit = 0; polymost_drawpoly(fpxy,n,method);
        if (!nofog) polymost_setFogEnabled(true);
    }

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    polymost_setClamp(0);

#ifdef POLYMER
    if (videoGetRenderMode() == REND_POLYMER)
    {
# ifdef USE_GLEXT
        r_detailmapping = olddetailmapping;
        r_glowmapping = oldglowmapping;
# endif
        polymer_postrotatesprite();
        pr_normalmapping = oldnormalmapping;
    }
#endif
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    globalpicnum = ogpicnum;
    globalshade  = ogshade;
    globalpal    = ogpal;
    ghalfx       = oghalfx;
    ghalfy       = oghalfy;
    grhalfxdown10 = ogrhalfxdown10;
    grhalfxdown10x = ogrhalfxdown10x;
    ghoriz       = oghoriz;
    frameoffset  = ofoffset;
    gchang = ogchang;
    gshang = ogshang;
    gctang = ogctang;
    gstang = ogstang;
    gvrcorrection = ogvrcorrection;

    polymost_identityrotmat();
}

static float trapextx[2];
static void drawtrap(float x0, float x1, float y0, float x2, float x3, float y1)
{
    if (y0 == y1) return;

    float px[4], py[4];
    int32_t n = 3;

    px[0] = x0; py[0] = y0;  py[2] = y1;
    if (x0 == x1) { px[1] = x3; py[1] = y1; px[2] = x2; }
    else if (x2 == x3) { px[1] = x1; py[1] = y0; px[2] = x3; }
    else               { px[1] = x1; py[1] = y0; px[2] = x3; px[3] = x2; py[3] = y1; n = 4; }

    glBegin(GL_TRIANGLE_FAN);
    for (bssize_t i=0; i<n; i++)
    {
        px[i] = min(max(px[i],trapextx[0]),trapextx[1]);
        glTexCoord2f(px[i]*xtex.u + py[i]*ytex.u + otex.u,
                      px[i]*xtex.v + py[i]*ytex.v + otex.v);
        glVertex2f(px[i],py[i]);
    }
    glEnd();
}

static void tessectrap(const float *px, const float *py, const int32_t *point2, int32_t numpoints)
{
    float x0, x1, m0, m1;
    int32_t i, j, k, z, i0, i1, i2, i3, npoints, gap, numrst;

    static int32_t allocpoints = 0, *slist = 0, *npoint2 = 0;
    typedef struct { float x, y, xi; int32_t i; } raster;
    static raster *rst = 0;
    if (numpoints+16 > allocpoints) //16 for safety
    {
        allocpoints = numpoints+16;
        rst = (raster *)Xrealloc(rst,allocpoints*sizeof(raster));
        slist = (int32_t *)Xrealloc(slist,allocpoints*sizeof(int32_t));
        npoint2 = (int32_t *)Xrealloc(npoint2,allocpoints*sizeof(int32_t));
    }

    //Remove unnecessary collinear points:
    for (i=0; i<numpoints; i++) npoint2[i] = point2[i];
    npoints = numpoints; z = 0;
    for (i=0; i<numpoints; i++)
    {
        j = npoint2[i]; if ((i < numpoints-1) && (point2[i] < i)) z = 3;
        if (j < 0) continue;
        k = npoint2[j];
        m0 = (px[j]-px[i])*(py[k]-py[j]);
        m1 = (py[j]-py[i])*(px[k]-px[j]);
        if (m0 < m1) { z |= 1; continue; }
        if (m0 > m1) { z |= 2; continue; }
        npoint2[i] = k; npoint2[j] = -1; npoints--; i--; //collinear
    }
    if (!z) return;
    trapextx[0] = trapextx[1] = px[0];
    for (i=j=0; i<numpoints; i++)
    {
        if (npoint2[i] < 0) continue;
        if (px[i] < trapextx[0]) trapextx[0] = px[i];
        if (px[i] > trapextx[1]) trapextx[1] = px[i];
        slist[j++] = i;
    }
    if (z != 3) //Simple polygon... early out
    {
        glBegin(GL_TRIANGLE_FAN);
        for (i=0; i<npoints; i++)
        {
            j = slist[i];
            glTexCoord2f(px[j]*xtex.u + py[j]*ytex.u + otex.u,
                          px[j]*xtex.v + py[j]*ytex.v + otex.v);
            glVertex2f(px[j],py[j]);
        }
        glEnd();
        return;
    }

    //Sort points by y's
    for (gap=(npoints>>1); gap; gap>>=1)
        for (i=0; i<npoints-gap; i++)
            for (j=i; j>=0; j-=gap)
            {
                if (py[npoint2[slist[j]]] <= py[npoint2[slist[j+gap]]]) break;
                k = slist[j]; slist[j] = slist[j+gap]; slist[j+gap] = k;
            }

    numrst = 0;
    for (z=0; z<npoints; z++)
    {
        i0 = slist[z]; i1 = npoint2[i0]; if (py[i0] == py[i1]) continue;
        i2 = i1; i3 = npoint2[i1];
        if (py[i1] == py[i3]) { i2 = i3; i3 = npoint2[i3]; }

        //i0        i3
        //  \      /
        //   i1--i2
        //  /      \ ~
        //i0        i3

        if ((py[i1] < py[i0]) && (py[i2] < py[i3])) //Insert raster
        {
            for (i=numrst; i>0; i--)
            {
                if (rst[i-1].xi*(py[i1]-rst[i-1].y) + rst[i-1].x < px[i1]) break;
                rst[i+1] = rst[i-1];
            }
            numrst += 2;

            if (i&1) //split inside area
            {
                j = i-1;

                x0 = (py[i1] - rst[j  ].y)*rst[j  ].xi + rst[j  ].x;
                x1 = (py[i1] - rst[j+1].y)*rst[j+1].xi + rst[j+1].x;
                drawtrap(rst[j].x,rst[j+1].x,rst[j].y,x0,x1,py[i1]);
                rst[j  ].x = x0; rst[j  ].y = py[i1];
                rst[j+3].x = x1; rst[j+3].y = py[i1];
            }

            m0 = (px[i0]-px[i1]) / (py[i0]-py[i1]);
            m1 = (px[i3]-px[i2]) / (py[i3]-py[i2]);
            j = ((px[i1] > px[i2]) || ((i1 == i2) && (m0 >= m1))) + i;
            k = (i<<1)+1 - j;
            rst[j].i = i0; rst[j].xi = m0; rst[j].x = px[i1]; rst[j].y = py[i1];
            rst[k].i = i3; rst[k].xi = m1; rst[k].x = px[i2]; rst[k].y = py[i2];
        }
        else
        {
            //NOTE:don't count backwards!
            if (i1 == i2) { for (i=0; i<numrst; i++) if (rst[i].i == i1) break; }
            else { for (i=0; i<numrst; i++) if ((rst[i].i == i1) || (rst[i].i == i2)) break; }
            j = i&~1;

            if ((py[i1] > py[i0]) && (py[i2] > py[i3])) //Delete raster
            {
                for (; j<=i+1; j+=2)
                {
                    x0 = (py[i1] - rst[j  ].y)*rst[j  ].xi + rst[j  ].x;
                    if ((i == j) && (i1 == i2)) x1 = x0; else x1 = (py[i1] - rst[j+1].y)*rst[j+1].xi + rst[j+1].x;
                    drawtrap(rst[j].x,rst[j+1].x,rst[j].y,x0,x1,py[i1]);
                    rst[j  ].x = x0; rst[j  ].y = py[i1];
                    rst[j+1].x = x1; rst[j+1].y = py[i1];
                }
                numrst -= 2; for (; i<numrst; i++) rst[i] = rst[i+2];
            }
            else
            {
                x0 = (py[i1] - rst[j  ].y)*rst[j  ].xi + rst[j  ].x;
                x1 = (py[i1] - rst[j+1].y)*rst[j+1].xi + rst[j+1].x;
                drawtrap(rst[j].x,rst[j+1].x,rst[j].y,x0,x1,py[i1]);
                rst[j  ].x = x0; rst[j  ].y = py[i1];
                rst[j+1].x = x1; rst[j+1].y = py[i1];

                if (py[i0] < py[i3]) { rst[i].x = px[i2]; rst[i].y = py[i2]; rst[i].i = i3; }
                else { rst[i].x = px[i1]; rst[i].y = py[i1]; rst[i].i = i0; }
                rst[i].xi = (px[rst[i].i] - rst[i].x) / (py[rst[i].i] - py[i1]);
            }
        }
    }
}

void polymost_fillpolygon(int32_t npoints)
{
    polymost_outputGLDebugMessage(3, "polymost_fillpolygon(npoints:%d)", npoints);

    globvis2 = 0;
    polymost_setVisibility(globvis2);

    globalx1 = mulscale16(globalx1,xyaspect);
    globaly2 = mulscale16(globaly2,xyaspect);
    xtex.u = ((float)asm1) * (1.f / 4294967296.f);
    xtex.v = ((float)asm2) * (1.f / 4294967296.f);
    ytex.u = ((float)globalx1) * (1.f / 4294967296.f);
    ytex.v = ((float)globaly2) * (-1.f / 4294967296.f);
    otex.u = (fxdim * xtex.u + fydim * ytex.u) * -0.5f + fglobalposx * (1.f / 4294967296.f);
    otex.v = (fxdim * xtex.v + fydim * ytex.v) * -0.5f - fglobalposy * (1.f / 4294967296.f);

    //Convert int32_t to float (in-place)
    for (bssize_t i=0; i<npoints; ++i)
    {
        ((float *)rx1)[i] = ((float)rx1[i])*(1.0f/4096.f);
        ((float *)ry1)[i] = ((float)ry1[i])*(1.0f/4096.f);
    }

    if (!polymost2d) polymostSet2dView(); //disables blending, texturing, and depth testing
    glEnable(GL_ALPHA_TEST);
    pthtyp const * const pth = our_texcache_fetch(DAMETH_NOMASK | (videoGetRenderMode() == REND_POLYMOST && r_useindexedcolortextures ? DAMETH_INDEXED : 0));

    if (pth)
    {
        polymost_bindPth(pth);

        if (!(pth->flags & PTH_INDEXED))
            polymost_usePaletteIndexing(false);
    }

    polymost_updatePalette();

    float const f = getshadefactor(globalshade);

    uint8_t const maskprops = (globalorientation>>7)&DAMETH_MASKPROPS;
    handle_blend(maskprops > DAMETH_MASK, 0, maskprops == DAMETH_TRANS2);
    if (maskprops > DAMETH_MASK)
    {
        glEnable(GL_BLEND);
        glColor4f(f, f, f, float_trans(maskprops, 0));
    }
    else
    {
        glDisable(GL_BLEND);
        glColor3f(f, f, f);
    }

    tessectrap((float *)rx1,(float *)ry1,xb1,npoints);

    if (pth && !(pth->flags & PTH_INDEXED))
    {
        // restore palette usage if we were just rendering a non-indexed color texture
        polymost_usePaletteIndexing(true);
    }
}

int32_t polymost_drawtilescreen(int32_t tilex, int32_t tiley, int32_t wallnum, int32_t dimen, int32_t tilezoom,
                                int32_t usehitile, uint8_t *loadedhitile)
{
    float xdime, ydime, xdimepad, ydimepad, scx, scy, ratio = 1.f;
    int32_t i;
    pthtyp *pth;

    if (videoGetRenderMode() < REND_POLYMOST || !in3dmode())
        return -1;

    polymost_outputGLDebugMessage(3, "polymost_drawtilescreen(tilex:%d, tiley:%d, wallnum:%d, dimen:%d, tilezoom:%d, usehitile:%d, loadedhitile:%p)",
                                  tilex, tiley, wallnum, dimen, tilezoom, usehitile, loadedhitile);

    if (!glinfo.texnpot)
    {
        i = (1<<(picsiz[wallnum]&15)); if (i < tilesiz[wallnum].x) i += i; xdimepad = (float)i;
        i = (1<<(picsiz[wallnum]>>4)); if (i < tilesiz[wallnum].y) i += i; ydimepad = (float)i;
    }
    else
    {
        xdimepad = (float)tilesiz[wallnum].x;
        ydimepad = (float)tilesiz[wallnum].y;
    }
    xdime = (float)tilesiz[wallnum].x; xdimepad = xdime/xdimepad;
    ydime = (float)tilesiz[wallnum].y; ydimepad = ydime/ydimepad;

    if ((xdime <= dimen) && (ydime <= dimen))
    {
        scx = xdime;
        scy = ydime;
    }
    else
    {
        scx = (float)dimen;
        scy = (float)dimen;
        if (xdime < ydime)
            scx *= xdime/ydime;
        else
            scy *= ydime/xdime;
    }

    int32_t const ousehightile = usehightile;
    usehightile = usehitile && usehightile;
    pth = texcache_fetch(wallnum, 0, 0, DAMETH_CLAMPED | (videoGetRenderMode() == REND_POLYMOST && r_useindexedcolortextures ? DAMETH_INDEXED : 0));
    if (usehightile)
        loadedhitile[wallnum>>3] |= pow2char[wallnum&7];
    usehightile = ousehightile;

    if (pth)
    {
        polymost_bindPth(pth);

        if (!(pth->flags & PTH_INDEXED))
            polymost_usePaletteIndexing(false);
    }

    globalshade = 0;
    polymost_updatePalette();

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);

    if (tilezoom)
    {
        if (scx > scy) ratio = dimen/scx;
        else ratio = dimen/scy;
    }

    glColor3f(1,1,1);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0,              0); glVertex2f((float)tilex            ,(float)tiley);
    glTexCoord2f(xdimepad,       0); glVertex2f((float)tilex+(scx*ratio),(float)tiley);
    glTexCoord2f(xdimepad,ydimepad); glVertex2f((float)tilex+(scx*ratio),(float)tiley+(scy*ratio));
    glTexCoord2f(0,       ydimepad); glVertex2f((float)tilex            ,(float)tiley+(scy*ratio));
    glEnd();

    if (pth && !(pth->flags & PTH_INDEXED))
    {
        // restore palette usage if we were just rendering a non-indexed color texture
        polymost_usePaletteIndexing(true);
    }

    return 0;
}

static int32_t gen_font_glyph_tex(void)
{
    // construct a 256x128 texture for the font glyph matrix

    glGenTextures(1,&polymosttext);

    if (!polymosttext) return -1;

    char * const tbuf = (char *)Xmalloc(256*128*4);

    Bmemset(tbuf, 0, 256*128*4);

    char * cptr = (char *)textfont;

    for (bssize_t h=0; h<256; h++)
    {
        char *tptr = tbuf + (h%32)*8*4 + (h/32)*256*8*4;
        for (bssize_t i=0; i<8; i++)
        {
            for (bssize_t j=0; j<8; j++)
            {
                if (cptr[h*8+i] & pow2char[7-j])
                {
                    Bmemset(tptr+4*j, 255, 4);
                }
            }
            tptr += 256*4;
        }
    }

    cptr = (char *)smalltextfont;

    for (bssize_t h=0; h<256; h++)
    {
        char *tptr = tbuf + 256*64*4 + (h%32)*8*4 + (h/32)*256*8*4;
        for (bssize_t i=1; i<7; i++)
        {
            for (bssize_t j=2; j<6; j++)
            {
                if (cptr[h*8+i] & pow2char[7-j])
                {
                    Bmemset(tptr+4*(j-2), 255, 4);
                }
            }
            tptr += 256*4;
        }
    }

    glBindTexture(GL_TEXTURE_2D, polymosttext);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,128,0,GL_RGBA,GL_UNSIGNED_BYTE,(GLvoid *)tbuf);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    Xfree(tbuf);

    return 0;
}

int32_t polymost_printtext256(int32_t xpos, int32_t ypos, int16_t col, int16_t backcol, const char *name, char fontsize)
{
    int const arbackcol = (unsigned)backcol < 256 ? backcol : 0;

    polymost_outputGLDebugMessage(3, "polymost_printtext256(xpos:%d, ypos:%d, col:%hd, backcol:%hd, name:%p, fontsize:%hhu)",
                                      xpos, ypos, col, backcol, name, fontsize);

    // FIXME?
    if (col < 0)
        col = 0;

    palette_t p, b;

    bricolor(&p, col);
    bricolor(&b, arbackcol);

    if (videoGetRenderMode() < REND_POLYMOST || !in3dmode() || (!polymosttext && gen_font_glyph_tex() < 0))
        return -1;

    glBindTexture(GL_TEXTURE_2D, polymosttext);
    polymost_setTexturePosSize({0, 0, 1, 1});

    polymost_usePaletteIndexing(false);

    polymostSet2dView();	// disables blending, texturing, and depth testing

    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_FALSE);	// disable writing to the z-buffer

//    glPushAttrib(GL_POLYGON_BIT|GL_ENABLE_BIT);
    // XXX: Don't fogify the OSD text in Mapster32 with r_usenewshading >= 2.
    polymost_setFogEnabled(false);
    // We want to have readable text in wireframe mode, too:
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    lastglpolygonmode = 0;

    if (backcol >= 0)
    {
        int const c = Bstrlen(name);

        glColor4ub(b.r,b.g,b.b,255);

        glBegin(GL_QUADS);

        glVertex2i(xpos,ypos);
        glVertex2i(xpos,ypos+(fontsize?6:8));
        glVertex2i(xpos+(c<<(3-fontsize)), ypos+(fontsize ? 6 : 8));
        glVertex2i(xpos+(c<<(3-fontsize)), ypos);

        glEnd();
    }

    glEnable(GL_BLEND);
    glColor4ub(p.r,p.g,p.b,255);

    vec2f_t const tc = { fontsize ? (4.f / 256.f) : (8.f / 256.f),
                         fontsize ? (6.f / 128.f) : (8.f / 128.f) };

    glBegin(GL_QUADS);

    for (bssize_t c=0; name[c]; ++c)
    {
        if (name[c] == '^' && isdigit(name[c+1]))
        {
            char smallbuf[8];
            int bi = 0;

            while (isdigit(name[c+1]) && bi<3)
            {
                smallbuf[bi++]=name[c+1];
                c++;
            }

            smallbuf[bi++] = 0;

            if (col)
            {
                col = Batol(smallbuf);

                if ((unsigned) col >= 256)
                    col = 0;
            }

            bricolor(&p, col);

            glColor4ub(p.r, p.g, p.b, 255);

            continue;
        }

        vec2f_t const t = { (float)(name[c] % 32) * (1.0f / 32.f),
                            (float)((name[c] / 32) + (fontsize * 8)) * (1.0f / 16.f) };

        glTexCoord2f(t.x, t.y);
        glVertex2i(xpos, ypos);

        glTexCoord2f(t.x + tc.x, t.y);
        glVertex2i(xpos + (8 >> fontsize), ypos);

        glTexCoord2f(t.x + tc.x, t.y + tc.y);
        glVertex2i(xpos + (8 >> fontsize), ypos + (fontsize ? 6 : 8));

        glTexCoord2f(t.x, t.y + tc.y);
        glVertex2i(xpos, ypos + (fontsize ? 6 : 8));

        xpos += (8>>fontsize);
    }

    glEnd();

    glDepthMask(GL_TRUE);	// re-enable writing to the z-buffer

//    glPopAttrib();

    if (!nofog) polymost_setFogEnabled(true);

    polymost_usePaletteIndexing(true);

    return 0;
}

// Console commands by JBF
static int32_t gltexturemode(osdcmdptr_t parm)
{
    int32_t m;
    char *p;

    if (parm->numparms != 1)
    {
        OSD_Printf("Current texturing mode is %s\n", glfiltermodes[gltexfiltermode].name);
        OSD_Printf("  Vaild modes are:\n");
        for (m = 0; m < NUMGLFILTERMODES; m++)
            OSD_Printf("     %d - %s\n", m, glfiltermodes[m].name);

        return OSDCMD_OK;
    }

    m = Bstrtoul(parm->parms[0], &p, 10);
    if (p == parm->parms[0])
    {
        // string
        for (m = 0; m < NUMGLFILTERMODES; m++)
        {
            if (!Bstrcasecmp(parm->parms[0], glfiltermodes[m].name))
                break;
        }

        if (m == NUMGLFILTERMODES)
            m = gltexfiltermode;   // no change
    }
    else
    {
        m = clamp(m, 0, NUMGLFILTERMODES-1);
    }

    gltexfiltermode = m;
    gltexapplyprops();

    OSD_Printf("Texture filtering mode changed to %s\n", glfiltermodes[gltexfiltermode].name);

    return OSDCMD_OK;
}

static int osdcmd_cvar_set_polymost(osdcmdptr_t parm)
{
    int32_t r = osdcmd_cvar_set(parm);

    if (xdim == 0 || ydim == 0 || bpp == 0) // video not set up yet
    {
        if (r == OSDCMD_OK)
        {
#ifdef POLYMER
            if (!Bstrcasecmp(parm->name, "r_pr_maxlightpasses"))
                pr_maxlightpasses = r_pr_maxlightpasses;
#endif
        }

        return r;
    }

    if (r == OSDCMD_OK)
    {
        if (!Bstrcasecmp(parm->name, "r_vsync"))
            vsync = videoSetVsync(vsync);
        else if (!Bstrcasecmp(parm->name, "r_downsize"))
        {
            if (r_downsizevar == -1)
                r_downsizevar = r_downsize;

            if (in3dmode() && r_downsize != r_downsizevar)
            {
                texcache_invalidate();
                videoResetMode();
                if (videoSetGameMode(fullscreen,xres,yres,bpp,upscalefactor))
                    OSD_Printf("restartvid: Reset failed...\n");
            }

            r_downsizevar = r_downsize;
        }
        else if (!Bstrcasecmp(parm->name, "r_anisotropy"))
            gltexapplyprops();
        else if (!Bstrcasecmp(parm->name, "r_texfilter"))
            gltexturemode(parm);
        else if (!Bstrcasecmp(parm->name, "r_usenewshading"))
            glFogi(GL_FOG_MODE, (r_usenewshading < 2) ? GL_EXP2 : GL_LINEAR);
#ifdef POLYMER
        else if (!Bstrcasecmp(parm->name, "r_pr_maxlightpasses"))
        {
            if (pr_maxlightpasses != r_pr_maxlightpasses)
            {
                polymer_invalidatelights();
                pr_maxlightpasses = r_pr_maxlightpasses;
            }
        }
#endif
    }

    return r;
}

void polymost_initosdfuncs(void)
{
    uint32_t i;

    static osdcvardata_t cvars_polymost[] =
    {
#if 0
        { "r_enablepolymost2","enable/disable polymost2",(void *) &r_enablepolymost2, CVAR_BOOL, 0, 0 }, //POGO: temporarily disable this variable
        { "r_pogoDebug","",(void *) &r_pogoDebug, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
        { "r_texfilter", "changes the texture filtering settings (may require restart)", (void *) &gltexfiltermode, CVAR_INT|CVAR_FUNCPTR, 0, 5 },
#endif
        { "r_polymostDebug","Set the verbosity of Polymost GL debug messages",(void *) &r_polymostDebug, CVAR_INT, 0, 3 },
#ifdef USE_GLEXT
        { "r_detailmapping","enable/disable detail mapping",(void *) &r_detailmapping, CVAR_BOOL, 0, 1 },
        { "r_glowmapping","enable/disable glow mapping",(void *) &r_glowmapping, CVAR_BOOL, 0, 1 },
#endif
#ifndef EDUKE32_GLES
        { "r_memcache","enable/disable texture cache memory cache",(void *) &glusememcache, CVAR_BOOL, 0, 1 },
        { "r_polygonmode","debugging feature",(void *) &r_polygonmode, CVAR_INT | CVAR_NOSAVE, 0, 3 },
        { "r_texcache","enable/disable OpenGL compressed texture cache",(void *) &glusetexcache, CVAR_INT, 0, 2 },
#endif
        { "r_animsmoothing","enable/disable model animation smoothing",(void *) &r_animsmoothing, CVAR_BOOL, 0, 1 },
        { "r_anisotropy", "changes the OpenGL texture anisotropy setting", (void *) &glanisotropy, CVAR_INT|CVAR_FUNCPTR, 0, 16 },
        { "r_downsize","controls downsizing factor (quality) for hires textures",(void *) &r_downsize, CVAR_INT|CVAR_FUNCPTR, 0, 5 },
        { "r_fullbrights","enable/disable fullbright textures",(void *) &r_fullbrights, CVAR_BOOL, 0, 1 },
        { "r_hightile","enable/disable hightile texture rendering",(void *) &usehightile, CVAR_BOOL, 0, 1 },
        { "r_models", "enable/disable model rendering", (void *)&usemodels, CVAR_BOOL, 0, 1 },
        { "r_nofog", "enable/disable GL fog", (void *)&nofog, CVAR_BOOL, 0, 1},
        { "r_npotwallmode", "enable/disable emulation of walls with non-power-of-two height textures (Polymost, r_hightile 0)",
          (void *) &r_npotwallmode, CVAR_INT | CVAR_NOSAVE, 0, 2 },
        { "r_parallaxskyclamping","enable/disable parallaxed floor/ceiling sky texture clamping", (void *) &r_parallaxskyclamping, CVAR_BOOL, 0, 1 },
        { "r_parallaxskypanning","enable/disable parallaxed floor/ceiling panning when drawing a parallaxing sky", (void *) &r_parallaxskypanning, CVAR_BOOL, 0, 1 },
        { "r_projectionhack", "enable/disable projection hack", (void *) &glprojectionhacks, CVAR_INT, 0, 2 },
        { "r_shadeinterpolate", "enable/disable shade interpolation", (void *) &r_shadeinterpolate, CVAR_BOOL, 0, 1 },
        { "r_shadescale","multiplier for shading",(void *) &shadescale, CVAR_FLOAT, 0, 10 },
        { "r_shadescale_unbounded","enable/disable allowance of complete blackness",(void *) &shadescale_unbounded, CVAR_BOOL, 0, 1 },
        { "r_texcompr","enable/disable OpenGL texture compression: 0: off  1: hightile only  2: ART and hightile",(void *) &glusetexcompr, CVAR_INT, 0, 2 },
        { "r_texturemaxsize","changes the maximum OpenGL texture size limit",(void *) &gltexmaxsize, CVAR_INT | CVAR_NOSAVE, 0, 4096 },
        { "r_texturemiplevel","changes the highest OpenGL mipmap level used",(void *) &gltexmiplevel, CVAR_INT, 0, 6 },
        { "r_useindexedcolortextures", "enable/disable indexed color texture rendering", (void *) &r_useindexedcolortextures, CVAR_BOOL, 0, 1 },
        { "r_usenewshading", "visibility/fog code: 0: orig. Polymost   1: 07/2011   2: linear 12/2012   3: no neg. start 03/2014   4: base constant on shade table 11/2017",
          (void *) &r_usenewshading, CVAR_INT|CVAR_FUNCPTR, 0, 4 },
        { "r_usetileshades", "enable/disable apply shade tables to art tiles", (void *) &r_usetileshades, CVAR_BOOL, 0, 1 },

        { "r_vsync",
          "VSync mode:\n"
          "  -1: adaptive (video driver)\n"
          "   0: disabled\n"
          "   1: enabled (video driver)\n"
#if defined _WIN32 && defined RENDERTYPESDL
          "   2: KMT\n"
#endif
          ,
          (void *)&vsync, CVAR_INT | CVAR_FUNCPTR, -1, 2 },

        { "r_vertexarrays","enable/disable using vertex arrays when drawing models",(void *) &r_vertexarrays, CVAR_BOOL, 0, 1 },
        { "r_yshearing", "enable/disable y-shearing", (void*) &r_yshearing, CVAR_BOOL, 0, 1 },
        { "r_flatsky", "enable/disable flat skies", (void*)& r_flatsky, CVAR_BOOL, 0, 1 },
#ifdef USE_GLEXT
        { "r_vbocount","sets the number of Vertex Buffer Objects to use when drawing models",(void *) &r_vbocount, CVAR_INT, 1, 256 },
        { "r_persistentStreamBuffer","enable/disable persistent stream buffering (requires renderer restart)",(void *) &r_persistentStreamBuffer, CVAR_BOOL, 0, 1 },
        { "r_drawpolyVertsBufferLength","sets the size of the vertex buffer for polymost's streaming VBO rendering (requires renderer restart)",(void *) &r_drawpolyVertsBufferLength, CVAR_INT, MAX_DRAWPOLY_VERTS, 1000000 },
#endif
#ifdef POLYMER
        { "r_pr_artmapping", "enable/disable art mapping", (void *) &pr_artmapping, CVAR_BOOL | CVAR_INVALIDATEART, 0, 1 },
        { "r_pr_ati_fboworkaround", "enable this to workaround an ATI driver bug that causes sprite shadows to be square - you need to restart the renderer for it to take effect", (void *) &pr_ati_fboworkaround, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
        { "r_pr_ati_nodepthoffset", "enable this to workaround an ATI driver bug that causes sprite drawing to freeze the game on Radeon X1x00 hardware - you need to restart the renderer for it to take effect", (void *) &pr_ati_nodepthoffset, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
        { "r_pr_billboardingmode", "face sprite display method. 0: classic mode; 1: polymost mode", (void *) &pr_billboardingmode, CVAR_BOOL, 0, 1 },
        { "r_pr_buckets", "controls batching of primitives. 0: no batching. 1: buckets of materials.", (void *)&pr_buckets, CVAR_BOOL | CVAR_NOSAVE | CVAR_RESTARTVID, 0, 1 },
        { "r_pr_customaspect", "if non-zero, forces the 3D view aspect ratio", (void *) &pr_customaspect, CVAR_DOUBLE, 0, 3 },
        { "r_pr_gpusmoothing", "toggles model animation interpolation", (void *)&pr_gpusmoothing, CVAR_BOOL, 0, 1 },
        { "r_pr_highpalookups", "enable/disable highpalookups", (void *) &pr_highpalookups, CVAR_BOOL, 0, 1 },
        { "r_pr_hudangadd", "overriden HUD angadd; see r_pr_overridehud", (void *) &pr_hudangadd, CVAR_INT | CVAR_NOSAVE, -1024, 1024 },
        { "r_pr_hudfov", "overriden HUD fov; see r_pr_overridehud", (void *) &pr_hudfov, CVAR_INT | CVAR_NOSAVE, 0, 1023 },
        { "r_pr_hudxadd", "overriden HUD xadd; see r_pr_overridehud", (void *) &pr_hudxadd, CVAR_FLOAT | CVAR_NOSAVE, -100, 100 },
        { "r_pr_hudyadd", "overriden HUD yadd; see r_pr_overridehud", (void *) &pr_hudyadd, CVAR_FLOAT | CVAR_NOSAVE, -100, 100 },
        { "r_pr_hudzadd", "overriden HUD zadd; see r_pr_overridehud", (void *) &pr_hudzadd, CVAR_FLOAT | CVAR_NOSAVE, -100, 100 },
        { "r_pr_lighting", "enable/disable dynamic lights - restarts renderer", (void *) &pr_lighting, CVAR_INT | CVAR_RESTARTVID, 0, 2 },
        { "r_pr_maxlightpasses", "the maximal amount of lights a single object can by affected by", (void *) &r_pr_maxlightpasses, CVAR_INT|CVAR_FUNCPTR, 0, PR_MAXLIGHTS },
        { "r_pr_maxlightpriority", "lowering that value removes less meaningful lights from the scene", (void *) &pr_maxlightpriority, CVAR_INT, 0, PR_MAXLIGHTPRIORITY },
        { "r_pr_normalmapping", "enable/disable virtual displacement mapping", (void *) &pr_normalmapping, CVAR_BOOL, 0, 1 },
        { "r_pr_nullrender", "disable all draws when enabled, 2: disables updates too", (void *)&pr_nullrender, CVAR_INT | CVAR_NOSAVE, 0, 3 },
        { "r_pr_overridehud", "overrides hud model parameters with values from the pr_hud* cvars; use it to fine-tune DEF tokens", (void *) &pr_overridehud, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
        { "r_pr_overridemodelscale", "overrides model scale if non-zero; use it to fine-tune DEF tokens", (void *) &pr_overridemodelscale, CVAR_FLOAT | CVAR_NOSAVE, 0, 500 },
        { "r_pr_overrideparallax", "overrides parallax mapping scale and bias values with values from the pr_parallaxscale and pr_parallaxbias cvars; use it to fine-tune DEF tokens", (void *) &pr_overrideparallax, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
        { "r_pr_overridespecular", "overrides specular material power and factor values with values from the pr_specularpower and pr_specularfactor cvars; use it to fine-tune DEF tokens", (void *) &pr_overridespecular, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
        { "r_pr_parallaxbias", "overriden parallax mapping offset bias", (void *) &pr_parallaxbias, CVAR_FLOAT | CVAR_NOSAVE, -10, 10 },
        { "r_pr_parallaxscale", "overriden parallax mapping offset scale", (void *) &pr_parallaxscale, CVAR_FLOAT | CVAR_NOSAVE, -10, 10 },
        { "r_pr_shadowcount", "maximal amount of shadow emitting lights on screen - you need to restart the renderer for it to take effect", (void *) &pr_shadowcount, CVAR_INT, 0, 64 },
        { "r_pr_shadowdetail", "sets the shadow map resolution - you need to restart the renderer for it to take effect", (void *) &pr_shadowdetail, CVAR_INT, 0, 5 },
        { "r_pr_shadowfiltering", "enable/disable shadow edges filtering - you need to restart the renderer for it to take effect", (void *) &pr_shadowfiltering, CVAR_BOOL, 0, 1 },
        { "r_pr_shadows", "enable/disable dynamic shadows", (void *) &pr_shadows, CVAR_BOOL, 0, 1 },
        { "r_pr_specularfactor", "overriden specular material factor", (void *) &pr_specularfactor, CVAR_FLOAT | CVAR_NOSAVE, -10, 1000 },
        { "r_pr_specularmapping", "enable/disable specular mapping", (void *) &pr_specularmapping, CVAR_BOOL, 0, 1 },
        { "r_pr_specularpower", "overriden specular material power", (void *) &pr_specularpower, CVAR_FLOAT | CVAR_NOSAVE, -10, 1000 },
        { "r_pr_vbos", "contols Vertex Buffer Object usage. 0: no VBOs. 1: VBOs for map data. 2: VBOs for model data.", (void *) &pr_vbos, CVAR_INT | CVAR_RESTARTVID, 0, 2 },
        { "r_pr_verbosity", "verbosity level of the polymer renderer", (void *) &pr_verbosity, CVAR_INT, 0, 3 },
        { "r_pr_wireframe", "toggles wireframe mode", (void *) &pr_wireframe, CVAR_BOOL | CVAR_NOSAVE, 0, 1 },
#endif
    };

    for (i=0; i<ARRAY_SIZE(cvars_polymost); i++)
        OSD_RegisterCvar(&cvars_polymost[i], (cvars_polymost[i].flags & CVAR_FUNCPTR) ? osdcmd_cvar_set_polymost : osdcmd_cvar_set);
}

void polymost_precache(int32_t dapicnum, int32_t dapalnum, int32_t datype)
{
    // dapicnum and dapalnum are like you'd expect
    // datype is 0 for a wall/floor/ceiling and 1 for a sprite
    //    basically this just means walls are repeating
    //    while sprites are clamped

    if (videoGetRenderMode() < REND_POLYMOST) return;
    if ((dapalnum < (MAXPALOOKUPS - RESERVEDPALS)) && (palookup[dapalnum] == NULL)) return;//dapalnum = 0;

    //OSD_Printf("precached %d %d type %d\n", dapicnum, dapalnum, datype);
    hicprecaching = 1;
    texcache_fetch(dapicnum, dapalnum, 0, (datype & 1)*(DAMETH_CLAMPED|DAMETH_MASK));
    hicprecaching = 0;

    if (datype == 0 || !usemodels) return;

    int const mid = md_tilehasmodel(dapicnum, dapalnum);

    if (mid < 0 || models[mid]->mdnum < 2) return;

    int const surfaces = (models[mid]->mdnum == 3) ? ((md3model_t *)models[mid])->head.numsurfs : 0;

    for (int i = 0; i <= surfaces; i++)
        mdloadskin((md2model_t *)models[mid], 0, dapalnum, i);
}

#else /* if !defined USE_OPENGL */

#include "compat.h"

int32_t polymost_drawtilescreen(int32_t tilex, int32_t tiley, int32_t wallnum, int32_t dimen,
                                int32_t usehitile, uint8_t *loadedhitile)
{
    UNREFERENCED_PARAMETER(tilex);
    UNREFERENCED_PARAMETER(tiley);
    UNREFERENCED_PARAMETER(wallnum);
    UNREFERENCED_PARAMETER(dimen);
    UNREFERENCED_PARAMETER(usehitile);
    UNREFERENCED_PARAMETER(loadedhitile);
    return -1;
}

#endif

// vim:ts=4:sw=4:
