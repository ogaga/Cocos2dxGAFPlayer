#include "GAFPrecompiled.h"
#include "GAFStencilMaskSprite.h"
#include "GAFShaderManager.h"

#define USE_LAYERED_STENCIL 0

static bool compare_stencil_sprites(const void* p1, const void* p2)
{
    GAFSprite* sp1 = (GAFSprite*)p1;
    GAFSprite* sp2 = (GAFSprite*)p2;

    return sp1->getLocalZOrder() < sp2->getLocalZOrder();
}

GAFStencilMaskSprite::GAFStencilMaskSprite(int stencilLayer)
:
_maskedObjects(NULL),
_isReorderMaskedObjectsDirty(false),
m_stencilLayer(stencilLayer)
{
}

static std::map<cocos2d::Node *, GAFStencilMaskSprite *> _object2maskedContainer;

void GAFStencilMaskSprite::updateMaskContainerOf(cocos2d::Node * node)
{
    std::map<cocos2d::Node *, GAFStencilMaskSprite *>::iterator it = _object2maskedContainer.find(node);
    if (it != _object2maskedContainer.end())
    {
        it->second->invalidateMaskedObjectsOrder();
    }
}


bool GAFStencilMaskSprite::initWithTexture(cocos2d::Texture2D *pTexture, const cocos2d::Rect& rect, bool rotated)
{
    if (!GAFSprite::initWithTexture(pTexture, cocos2d::Rect(0, 0, rect.size.width, rect.size.height), rotated))
    {
        return false;
    }
    CC_SAFE_RELEASE(_maskedObjects);
    _maskedObjects = cocos2d::__Array::create();
    _maskedObjects->retain();
    setGLProgram(programShaderForMask());
    _isReorderMaskedObjectsDirty = false;
    return true;
}

GAFStencilMaskSprite::~GAFStencilMaskSprite()
{
    if (_maskedObjects)
    {
        for (int i = 0; i < _maskedObjects->count(); ++i)
        {
            cocos2d::Node * node = (cocos2d::Node*)_maskedObjects->getObjectAtIndex(i);
            std::map<cocos2d::Node *, GAFStencilMaskSprite *>::iterator it = _object2maskedContainer.find(node);
            if (it != _object2maskedContainer.end())
            {
                _object2maskedContainer.erase(it);
            }
        }
    }

    CC_SAFE_RELEASE(_maskedObjects);
}

void GAFStencilMaskSprite::visit(cocos2d::Renderer *renderer, const cocos2d::Mat4 &transform, uint32_t flags)
{
    GAFStencilMaskSprite::visit(renderer, transform, bool(flags & cocos2d::Node::FLAGS_TRANSFORM_DIRTY));
}

void GAFStencilMaskSprite::visit(cocos2d::Renderer *renderer, const cocos2d::Mat4 &transform, bool transformUpdated)
{
    GAFSprite::visit(renderer, transform, true);
    sortAllMaskedObjects();
    // Draw subobjects, assuming mask and object are on the same layer
    for (int i = 0; i < _maskedObjects->count(); ++i)
    {
        cocos2d::Node * object = (cocos2d::Node *)_maskedObjects->getObjectAtIndex(i);
        object->visit();
    }
#if USE_LAYERED_STENCIL
    _disableStencil();
#else
    glDisable(GL_STENCIL_TEST);
#endif

}

void GAFStencilMaskSprite::sortAllMaskedObjects()
{
    if (_isReorderMaskedObjectsDirty)
    {
        std::sort(_maskedObjects->data->arr,
            _maskedObjects->data->arr + _maskedObjects->data->num,
            compare_stencil_sprites);
        _isReorderMaskedObjectsDirty = false;
    }
}
void GAFStencilMaskSprite::_disableStencil()
{
    glStencilFunc(m_stencilState.currentStencilFunc, m_stencilState.currentStencilRef, m_stencilState.currentStencilValueMask);
    glStencilOp(m_stencilState.currentStencilFail, m_stencilState.currentStencilPassDepthFail, m_stencilState.currentStencilPassDepthPass);
    glStencilMask(m_stencilState.currentStencilWriteMask);

    if (!m_stencilState.currentStencilEnabled)
    {
        glDisable(GL_STENCIL_TEST);
    }

    // we are done using this layer, decrement
    m_stencilLayer = std::max(--m_stencilLayer, -1);
}

void GAFStencilMaskSprite::draw(cocos2d::Renderer *renderer, const cocos2d::Mat4 &transform, uint32_t flags)
{
    GAFStencilMaskSprite::draw(renderer, transform, bool(flags & cocos2d::Node::FLAGS_TRANSFORM_DIRTY));
}

void GAFStencilMaskSprite::draw(cocos2d::Renderer *renderer, const cocos2d::Mat4 &transform, bool transformUpdated)
{
    // Prepare stencil
#if USE_LAYERED_STENCIL
    m_stencilLayer = std::min(++m_stencilLayer, 255);
    // mask of the current layer (ie: for layer 3: 00000100)
    GLint mask_layer = 0x1 << m_stencilLayer;
    // mask of all layers less than the current (ie: for layer 3: 00000011)
    GLint mask_layer_l = mask_layer - 1;
    // mask of all layers less than or equal to the current (ie: for layer 3: 00000111)
    GLint mask_layer_le = mask_layer | mask_layer_l;

    // manually save the stencil state

    m_stencilState.currentStencilEnabled = GL_FALSE;
    m_stencilState.currentStencilWriteMask = UINT_MAX;
    m_stencilState.currentStencilFunc = GL_ALWAYS;
    m_stencilState.currentStencilRef = 0;
    m_stencilState.currentStencilValueMask = UINT_MAX;
    m_stencilState.currentStencilFail = GL_KEEP;
    m_stencilState.currentStencilPassDepthFail = GL_KEEP;
    m_stencilState.currentStencilPassDepthPass = GL_KEEP;
    m_stencilState.currentStencilEnabled = glIsEnabled(GL_STENCIL_TEST);


    glGetIntegerv(GL_STENCIL_WRITEMASK, (GLint *)&m_stencilState.currentStencilWriteMask);
    glGetIntegerv(GL_STENCIL_FUNC, (GLint *)&m_stencilState.currentStencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &m_stencilState.currentStencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, (GLint *)&m_stencilState.currentStencilValueMask);
    glGetIntegerv(GL_STENCIL_FAIL, (GLint *)&m_stencilState.currentStencilFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, (GLint *)&m_stencilState.currentStencilPassDepthFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, (GLint *)&m_stencilState.currentStencilPassDepthPass);


    // enable stencil use
    glEnable(GL_STENCIL_TEST);
    // check for OpenGL error while enabling stencil test

    CHECK_GL_ERROR_DEBUG();

    // all bits on the stencil buffer are readonly, except the current layer bit,
    // this means that operation like glClear or glStencilOp will be masked with this value

    glStencilMask(mask_layer);

    glClear(GL_STENCIL_BUFFER_BIT);

    // manually save the depth test state
    //GLboolean currentDepthTestEnabled = GL_TRUE;
    GLboolean currentDepthWriteMask = GL_TRUE;
    //currentDepthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &currentDepthWriteMask);


    // disable depth test while drawing the stencil
    //glDisable(GL_DEPTH_TEST);
    // disable update to the depth buffer while drawing the stencil,
    // as the stencil is not meant to be rendered in the real scene,
    // it should never prevent something else to be drawn,
    // only disabling depth buffer update should do

    glDepthMask(GL_FALSE);

    ///////////////////////////////////
    // CLEAR STENCIL BUFFER


    // manually clear the stencil buffer by drawing a fullscreen rectangle on it
    // setup the stencil test func like this:
    // for each pixel in the fullscreen rectangle
    //     never draw it into the frame buffer
    //     if not in inverted mode: set the current layer value to 0 in the stencil buffer
    //     if in inverted mode: set the current layer value to 1 in the stencil buffer

    glStencilFunc(GL_NEVER, mask_layer, mask_layer);
    glStencilOp(GL_ZERO, GL_KEEP, GL_KEEP);


    // draw a fullscreen solid rectangle to clear the stencil buffer
    //ccDrawSolidRect(CCPointZero, ccpFromSize([[CCDirector sharedDirector] winSize]), ccc4f(1, 1, 1, 1));

    cocos2d::DrawPrimitives::drawSolidRect(cocos2d::Vect::ZERO, cocos2d::Vect(cocos2d::Director::getInstance()->getWinSize()), cocos2d::Color4F(1, 1, 1, 1));


    ///////////////////////////////////
    // DRAW CLIPPING STENCIL

    // setup the stencil test func like this:
    // for each pixel in the stencil node
    //     never draw it into the frame buffer
    //     if not in inverted mode: set the current layer value to 1 in the stencil buffer
    //     if in inverted mode: set the current layer value to 0 in the stencil buffer

    glStencilFunc(GL_NEVER, mask_layer, mask_layer);
    glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);

    // Draw mask

    GAFSprite::draw(renderer, transform, transformUpdated);

    // setup the stencil test func like this:
    // for each pixel of this node and its childs
    //     if all layers less than or equals to the current are set to 1 in the stencil buffer
    //         draw the pixel and keep the current layer in the stencil buffer
    //     else
    //         do not draw the pixel but keep the current layer in the stencil buffer

    glStencilFunc(GL_EQUAL, mask_layer_le, mask_layer_le);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);


#else
    glEnable(GL_STENCIL_TEST);
    glClear(GL_STENCIL_BUFFER_BIT);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
    glStencilFunc(GL_ALWAYS, 1, 1);

    // Draw mask
    GAFSprite::draw(renderer, transform, transformUpdated);

    // Use stencil
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 1);

#endif
}

void GAFStencilMaskSprite::addMaskedObject(cocos2d::Node * anObject)
{
    std::map<cocos2d::Node *, GAFStencilMaskSprite *>::iterator it = _object2maskedContainer.find(anObject);

    GAFStencilMaskSprite * maskContainer = it != _object2maskedContainer.end() ? it->second : NULL;

    if (maskContainer)
    {
        maskContainer->removeMaskedObject(anObject);
    }

    it = _object2maskedContainer.find(anObject);

    maskContainer = it != _object2maskedContainer.end() ? it->second : NULL;

    if (maskContainer != this)
    {
        _object2maskedContainer[anObject] = this;
        _maskedObjects->addObject(anObject);
        _isReorderMaskedObjectsDirty = true;
    }
}

void GAFStencilMaskSprite::removeMaskedObject(cocos2d::Node * anObject)
{
    if (_maskedObjects->containsObject(anObject))
    {
        std::map<cocos2d::Node *, GAFStencilMaskSprite *>::iterator it = _object2maskedContainer.find(anObject);
        CCAssert(it != _object2maskedContainer.end(), "iterator must be valid");
        _object2maskedContainer.erase(it);
        _maskedObjects->removeObject(anObject);
        _isReorderMaskedObjectsDirty = true;
    }
}

void GAFStencilMaskSprite::invalidateMaskedObjectsOrder()
{
    _isReorderMaskedObjectsDirty = true;
}

cocos2d::GLProgram * GAFStencilMaskSprite::programShaderForMask()
{
    cocos2d::GLProgram *program = cocos2d::ShaderCache::getInstance()->getGLProgram(kGAFStencilMaskAlphaFilterProgramCacheKey);

    if (!program)
    {
#if CC_TARGET_PLATFORM == CC_PLATFORM_WP8 || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT && !defined(_DEBUG))
#include "ShadersPrecompiled/GAFPrecompiledShaders.h"
        program = new cocos2d::GLProgram();();
        program->autorelease();
        program->initWithPrecompiledProgramByteArray((const GLchar*)kGAFScrollLayerAlphaFilterProgramCacheKey,
            sizeof(kGAFScrollLayerAlphaFilterProgramCacheKey));
        program->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        program->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
        program->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        program->updateUniforms();
        CHECK_GL_ERROR_DEBUG();
        CCShaderCache::sharedShaderCache()->addProgram(program, kGAFStencilMaskAlphaFilterProgramCacheKey);
#else
        const char* frag = GAFShaderManager::getShader(GAFShaderManager::EFragmentShader::AlphaFilter);
        program = cocos2d::GLProgram::createWithByteArrays(cocos2d::ccPositionTextureColor_vert, frag);
        if (program)
        {
            program->bindAttribLocation(cocos2d::GLProgram::ATTRIBUTE_NAME_POSITION, cocos2d::GLProgram::VERTEX_ATTRIB_POSITION);
            program->bindAttribLocation(cocos2d::GLProgram::ATTRIBUTE_NAME_COLOR, cocos2d::GLProgram::VERTEX_ATTRIB_COLOR);
            program->bindAttribLocation(cocos2d::GLProgram::ATTRIBUTE_NAME_TEX_COORD, cocos2d::GLProgram::VERTEX_ATTRIB_TEX_COORDS);

            program->link();
            program->updateUniforms();
            CHECK_GL_ERROR_DEBUG();
            cocos2d::ShaderCache::getInstance()->addGLProgram(program, kGAFStencilMaskAlphaFilterProgramCacheKey);
        }
        else
        {
            CCLOGERROR("Cannot load program for programShaderForMask.");
            return NULL;
        }
#endif
    }

    program->use();
    return program;
}

void GAFStencilMaskSprite::updateStencilLayer(int newLayer)
{
    m_stencilLayer = newLayer;
}
