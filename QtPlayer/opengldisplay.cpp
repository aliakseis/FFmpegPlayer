#include "opengldisplay.h"

// https://github.com/MasterAler/SampleYUVRenderer

#include <QOpenGLShader>
#include <QOpenGLTexture>
#include <QCoreApplication>
#include <QResizeEvent>

#include <algorithm>
#include <atomic>
#include <mutex>


enum {
PROGRAM_VERTEX_ATTRIBUTE  = 0,
PROGRAM_TEXCOORD_ATTRIBUTE = 1,

ATTRIB_VERTEX = 0,
ATTRIB_TEXTURE = 1,
};

struct OpenGLDisplay::OpenGLDisplayImpl
{
    GLvoid*                 mBufYuv{nullptr};
    unsigned int mFrameSize{0};

    int mW{0};
    int mH{0};

    QOpenGLShader*          mVShader;
    QOpenGLShader*          mFShader;
    QOpenGLShaderProgram*   mShaderProgram;

    QOpenGLTexture*         mTextureY;
    QOpenGLTexture*         mTextureU;
    QOpenGLTexture*         mTextureV;

    GLuint                  id_y, id_u, id_v;
    int                     textureUniformY, textureUniformU, textureUniformV;
    GLsizei                 mVideoW, mVideoH;

    std::mutex m_mutex;

    float m_aspectRatio{ 0.75F };

    std::atomic_bool m_pendingUpdate = false;
};

/*************************************************************************/

OpenGLDisplay::OpenGLDisplay(QWidget* parent)
    : QOpenGLWidget(parent)
    , impl(new OpenGLDisplayImpl())
{
    connect(this, &OpenGLDisplay::display, this, &OpenGLDisplay::currentDisplay);
}

OpenGLDisplay::~OpenGLDisplay()
{
    delete[] reinterpret_cast<unsigned char*>(impl->mBufYuv);
}

void OpenGLDisplay::InitDrawBuffer(unsigned bsize)
{
    if (impl->mFrameSize < bsize)
    {
        delete[] reinterpret_cast<unsigned char*>(impl->mBufYuv);
        impl->mFrameSize = bsize;
        impl->mBufYuv = new unsigned char[bsize];
    }
}

//void OpenGLDisplay::DisplayVideoFrame(unsigned char *data, int frameWidth, int frameHeight)
//{
//    impl->mVideoW = frameWidth;
//    impl->mVideoH = frameHeight;
//    memcpy(impl->mBufYuv, data, impl->mFrameSize);
//    update();
//}

void OpenGLDisplay::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);

    /* Modern opengl rendering pipeline relies on shaders to handle incoming data.
     *  Shader: is a small function written in OpenGL Shading Language (GLSL).
     * GLSL is the language that makes up all OpenGL shaders.
     * The syntax of the specific GLSL language requires the reader to find relevant information. */

    // Initialize the vertex shader object
    impl->mVShader = new QOpenGLShader(QOpenGLShader::Vertex, this);

    //Vertex shader source
    const char *vsrc = "attribute vec4 vertexIn; \
        attribute vec2 textureIn; \
        varying vec2 textureOut;  \
        void main(void)           \
        {                         \
            gl_Position = vertexIn; \
            textureOut = textureIn; \
        }";

    //Compile the vertex shader program
    bool bCompile = impl->mVShader->compileSourceCode(vsrc);
    if(!bCompile)
    {
        throw OpenGlException();
    }

    // Initialize the fragment shader function yuv converted to rgb
    impl->mFShader = new QOpenGLShader(QOpenGLShader::Fragment, this);

    // Fragment shader source code

#ifdef QT_OPENGL_ES_2
    const char *fsrc = "precision mediump float; \
    varying vec2 textureOut; \
        uniform sampler2D tex_y; \
        uniform sampler2D tex_u; \
        uniform sampler2D tex_v; \
        void main(void) \
        { \
            vec3 yuv; \
            vec3 rgb; \
            yuv.x = texture2D(tex_y, textureOut).r; \
            yuv.y = texture2D(tex_u, textureOut).r - 0.5; \
            yuv.z = texture2D(tex_v, textureOut).r - 0.5; \
            rgb = mat3( 1,       1,         1, \
                        0,       -0.39465,  2.03211, \
                        1.13983, -0.58060,  0) * yuv; \
            gl_FragColor = vec4(rgb, 1); \
        }";
#else
    const char *fsrc = "varying vec2 textureOut; \
    uniform sampler2D tex_y; \
    uniform sampler2D tex_u; \
    uniform sampler2D tex_v; \
    void main(void) \
    { \
        vec3 yuv; \
        vec3 rgb; \
        yuv.x = texture2D(tex_y, textureOut).r; \
        yuv.y = texture2D(tex_u, textureOut).r - 0.5; \
        yuv.z = texture2D(tex_v, textureOut).r - 0.5; \
        rgb = mat3( 1,       1,         1, \
                    0,       -0.39465,  2.03211, \
                    1.13983, -0.58060,  0) * yuv; \
        gl_FragColor = vec4(rgb, 1); \
    }";
#endif

    bCompile = impl->mFShader->compileSourceCode(fsrc);
    if(!bCompile)
    {
        throw OpenGlException();
    }
    
    // Create a shader program container
    impl->mShaderProgram = new QOpenGLShaderProgram(this);
    // Add the fragment shader to the program container
    impl->mShaderProgram->addShader(impl->mFShader);
    // Add a vertex shader to the program container
    impl->mShaderProgram->addShader(impl->mVShader);
    // Bind the property vertexIn to the specified location ATTRIB_VERTEX, this property
    // has a declaration in the vertex shader source
    impl->mShaderProgram->bindAttributeLocation("vertexIn", ATTRIB_VERTEX);
    // Bind the attribute textureIn to the specified location ATTRIB_TEXTURE, the attribute 
    // has a declaration in the vertex shader source
    impl->mShaderProgram->bindAttributeLocation("textureIn", ATTRIB_TEXTURE);
    //Link all the shader programs added to
    impl->mShaderProgram->link();
    //activate all links
    impl->mShaderProgram->bind();
    // Read the position of the data variables tex_y, tex_u, tex_v in the shader, the declaration
    // of these variables can be seen in 
    // fragment shader source 
    impl->textureUniformY = impl->mShaderProgram->uniformLocation("tex_y");
    impl->textureUniformU = impl->mShaderProgram->uniformLocation("tex_u");
    impl->textureUniformV = impl->mShaderProgram->uniformLocation("tex_v");
    
    //Vertex matrix
    static const GLfloat vertexVertices[] = {
        -1.0F, -1.0F,
         1.0F, -1.0F,
         -1.0F, 1.0F,
         1.0F, 1.0F,
    };
    
    //Texture matrix
    static const GLfloat textureVertices[] = {
        0.0F,  1.0F,
        1.0F,  1.0F,
        0.0F,  0.0F,
        1.0F,  0.0F,
    };

    // Set the value of the vertex matrix of the attribute ATTRIB_VERTEX and format
    glVertexAttribPointer(ATTRIB_VERTEX, 2, GL_FLOAT, 0, 0, vertexVertices);
    // Set the texture matrix value and format of the attribute ATTRIB_TEXTURE
    glVertexAttribPointer(ATTRIB_TEXTURE, 2, GL_FLOAT, 0, 0, textureVertices);
    // Enable the ATTRIB_VERTEX attribute data, the default is off
    glEnableVertexAttribArray(ATTRIB_VERTEX);
    // Enable the ATTRIB_TEXTURE attribute data, the default is off
    glEnableVertexAttribArray(ATTRIB_TEXTURE);
    
    // Create y, u, v texture objects respectively    
    impl->mTextureY = new QOpenGLTexture(QOpenGLTexture::Target2D);
    impl->mTextureU = new QOpenGLTexture(QOpenGLTexture::Target2D);
    impl->mTextureV = new QOpenGLTexture(QOpenGLTexture::Target2D);
    impl->mTextureY->create();
    impl->mTextureU->create();
    impl->mTextureV->create();

    // Get the texture index value of the return y component
    impl->id_y = impl->mTextureY->textureId();
    // Get the texture index value of the returned u component
    impl->id_u = impl->mTextureU->textureId();
    // Get the texture index value of the returned v component
    impl->id_v = impl->mTextureV->textureId();

    glClearColor (0.3, 0.3, 0.3, 0.0); // set the background color
//    qDebug("addr=%x id_y = %d id_u=%d id_v=%d\n", this, impl->id_y, impl->id_u, impl->id_v);
}

void OpenGLDisplay::resizeGL(int w, int h)
{
    if(h == 0) {// prevents being divided by zero
        h = 1;// set the height to 1
    }

    impl->mW = w;
    impl->mH = h;
    update();
}

void OpenGLDisplay::paintGL()
{
    std::unique_lock<std::mutex> lock(impl->m_mutex);

    if (!impl->mBufYuv) {
        return;
    }

    // https://stackoverflow.com/questions/33059185/qopenglwidgets-resizegl-is-not-the-place-to-call-glviewport
    // Set the viewport
    glViewport(0, 0, impl->mW, impl->mH);

    // Load y data texture
    // Activate the texture unit GL_TEXTURE0
    glActiveTexture(GL_TEXTURE0);
    // Use the texture generated from y to generate texture
    glBindTexture(GL_TEXTURE_2D, impl->id_y);

    // Fixes abnormality with 174x100 yuv data
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

    // Use the memory mBufYuv data to create a real y data texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, impl->mVideoW, impl->mVideoH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, impl->mBufYuv);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Load u data texture
    glActiveTexture(GL_TEXTURE1);//Activate texture unit GL_TEXTURE1
    glBindTexture(GL_TEXTURE_2D, impl->id_u);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, impl->mVideoW/2, impl->mVideoH/2
                 , 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, static_cast<char*>(impl->mBufYuv) + impl->mVideoW * impl->mVideoH);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Load v data texture
    glActiveTexture(GL_TEXTURE2);//Activate texture unit GL_TEXTURE2
    glBindTexture(GL_TEXTURE_2D, impl->id_v);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, impl->mVideoW / 2, impl->mVideoH / 2
                 , 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, static_cast<char*>(impl->mBufYuv) + impl->mVideoW * impl->mVideoH * 5/4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Specify y texture to use the new value can only use 0, 1, 2, etc. to represent
    // the index of the texture unit, this is the place where opengl is not humanized
    //0 corresponds to the texture unit GL_TEXTURE0 1 corresponds to the
    // texture unit GL_TEXTURE1 2 corresponds to the texture unit GL_TEXTURE2
    glUniform1i(impl->textureUniformY, 0);
    // Specify the u texture to use the new value
    glUniform1i(impl->textureUniformU, 1);
    // Specify v texture to use the new value
    glUniform1i(impl->textureUniformV, 2);
    // Use the vertex array way to draw graphics
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFlush();
}

//////////////////////////////////////////////////////////////////////////////

void OpenGLDisplay::currentDisplay(unsigned int generation)
{
    if (!(impl->m_pendingUpdate.exchange(true)))
    {
        QMetaObject::invokeMethod(this, [this]
            {
                impl->m_pendingUpdate = false;
                setUpdatesEnabled(true);
                update();
            });
    }
    finishedDisplayingFrame(generation);
}

void OpenGLDisplay::updateFrame(IFrameDecoder* decoder)
{
    FrameRenderingData data;
    if (!decoder->getFrameRenderingData(&data))
    {
        return;
    }

    std::unique_lock<std::mutex> lock(impl->m_mutex);

    impl->m_aspectRatio = float(data.height) / data.width;

    impl->mVideoW = data.width;
    impl->mVideoH = data.height;

    InitDrawBuffer(data.height * data.width * 3 / 2);

    auto dst = reinterpret_cast<unsigned char*>(impl->mBufYuv);
    int width = data.width;
    int height = data.height;
    for (int i = 0; i < 3; ++i)
    {
        auto src = data.image[i];
        for (int j = 0; j < height; ++j)
        {
            memcpy(dst, src, width);
            dst += width;
            src += data.pitch[i];
        }
        width = data.width / 2;
        height = data.height / 2;
    }
}


void OpenGLDisplay::showPicture(const QImage& img)
{
    if (img.isNull()) {
        return;
    }

    if(img.format()!=QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB888)
    {
       Q_ASSERT(false && "Wrong image format\n");
       return;
    }

    const auto step = (img.format() == QImage::Format_RGB888)? 3 : 4;

    const int width = img.width() & ~1;
    const int height = img.height() & ~1;

    std::unique_lock<std::mutex> lock(impl->m_mutex);

    impl->m_aspectRatio = float(height) / width;

    // RGB to YUV420
    impl->mVideoW = width;
    impl->mVideoH = height;

    InitDrawBuffer(height * width * 3 / 2);

    int size = width*height;
    // Y
    for(unsigned y=0;y<height;y++)
    {
       const auto *s = img.scanLine(y);
       unsigned char *d = reinterpret_cast<unsigned char*>(impl->mBufYuv) + y*width;

       for(unsigned x=0;x<width;x++)
       {
          unsigned int r=s[2];
          unsigned int g=s[1];
          unsigned int b=s[0];

          unsigned Y = std::min((r*2104 + g*4130 + b*802 + 4096 + 131072) >> 13, 235U);
          *d = Y;

          d++;
          s+=step;
       }
    }

    // U,V
    const unsigned int ss = img.bytesPerLine();
    for(unsigned y=0;y<height;y+=2)
    {
       const auto *s = img.scanLine(y);
       unsigned char *d = reinterpret_cast<unsigned char*>(impl->mBufYuv) + size+y/2*width/2;

       for(unsigned x=0;x<width;x+=2)
       {
          // Cr = 128 + 1/256 * ( 112.439 * R'd -  94.154 * G'd -  18.285 * B'd)
          // Cb = 128 + 1/256 * (- 37.945 * R'd -  74.494 * G'd + 112.439 * B'd)

          // Get the average RGB in a 2x2 block
          int r=(s[2] + s[step+2] + s[ss+2] + s[ss+step+2] + 2) >> 2;
          int g=(s[1] + s[step+1] + s[ss+1] + s[ss+step+1] + 2) >> 2;
          int b=(s[0] + s[step] + s[ss+0] + s[ss+step] + 2) >> 2;

          int Cb = std::clamp((-1214*r - 2384*g + 3598*b + 4096 + 1048576)>>13, 16, 240);
          int Cr = std::clamp((3598*r - 3013*g - 585*b + 4096 + 1048576)>>13, 16, 240);

          *d = Cb;
          *(d+size/4) = Cr;

          d++;
          s+=step * 2;
       }
    }

    lock.unlock();
    emit update();
}

void OpenGLDisplay::showPicture(const QPixmap& picture)
{
    showPicture(picture.toImage());
}

void OpenGLDisplay::drawFrame(IFrameDecoder* decoder, unsigned int generation)
{
    emit display(generation);
}

void OpenGLDisplay::decoderClosing()
{
}

float OpenGLDisplay::aspectRatio() const { return impl->m_aspectRatio; }
