#pragma once
// =============================================================
//  BACKROOMS - UI / HUD system (immediate-mode, OpenGL 3.3)
//  ------------------------------------------------------------
//  A small commercial-quality 2D overlay drawn in an orthographic
//  pass on top of the post-processed scene:
//    - rounded glass panels
//    - health / stamina bars with gradient fill + labels
//    - level + XP readout and XP progress bar
//    - status-effect chips (INJURED / WINDED / SPRINT)
//    - crosshair
//    - full pause menu (Resume / Settings / Graphics / Audio /
//      Controls / Exit To Menu / Quit) with hover + sub-pages
//  Text is rendered from the shared 5x7 bitmap font baked into a
//  single-channel atlas at startup.
// =============================================================
#include <glad/gl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include "textures.h"   // for tex::glyph

namespace ui {

// ---- glyph atlas (16x6 grid of 5x7 cells scaled up) ----
struct Atlas { GLuint tex=0; int cell=0; };

inline const char* charSet(){
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-!/:%";
}

inline Atlas buildFontAtlas(){
    const char* set=charSet();
    int n=(int)strlen(set);
    const int CELL=16;          // atlas cell px
    const int GX=5,GY=7;
    int cols=n;
    int W=cols*CELL, H=CELL;
    std::vector<uint8_t> px((size_t)W*H,0);
    for(int i=0;i<n;i++){
        const char* g=tex::glyph(set[i]);
        for(int y=0;y<GY;y++)for(int x=0;x<GX;x++){
            if(g[y*GX+x]!='1') continue;
            // scale 5x7 into CELL with 1px padding
            int x0=i*CELL + 2 + (int)(x*(CELL-4)/(float)GX);
            int x1=i*CELL + 2 + (int)((x+1)*(CELL-4)/(float)GX);
            int y0=2 + (int)(y*(CELL-4)/(float)GY);
            int y1=2 + (int)((y+1)*(CELL-4)/(float)GY);
            for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
                if(xx<0||xx>=W||yy<0||yy>=H) continue;
                px[yy*W+xx]=255;
            }
        }
    }
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RED,W,H,0,GL_RED,GL_UNSIGNED_BYTE,px.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    Atlas a; a.tex=t; a.cell=CELL; return a;
}

inline int charIndex(char c){
    if(c>='a'&&c<='z') c=c-'a'+'A';
    const char* set=charSet();
    const char* p=strchr(set,c);
    return p? (int)(p-set) : (int)(strchr(set,' ')-set);
}

// =============================================================
//  Renderer: owns a dynamic VBO and the UI shader uniforms.
// =============================================================
class Renderer {
public:
    void init(GLuint prog){
        prog_=prog;
        glGenVertexArrays(1,&vao_); glBindVertexArray(vao_);
        glGenBuffers(1,&vbo_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
        glBindVertexArray(0);
        atlas_=buildFontAtlas();
        uScreen_=glGetUniformLocation(prog_,"uScreen");
        uMode_  =glGetUniformLocation(prog_,"uMode");
        uColor_ =glGetUniformLocation(prog_,"uColor");
        uColor2_=glGetUniformLocation(prog_,"uColor2");
        uRect_  =glGetUniformLocation(prog_,"uRect");
        uRadius_=glGetUniformLocation(prog_,"uRadius");
        uTex_   =glGetUniformLocation(prog_,"uTex");
        uFill_  =glGetUniformLocation(prog_,"uFill");
        uTime_  =glGetUniformLocation(prog_,"uTime");
    }
    void begin(int w,int h,float time){
        w_=w; h_=h;
        glUseProgram(prog_);
        glUniform2f(uScreen_,(float)w,(float)h);
        glUniform1f(uTime_,time);
        glBindVertexArray(vao_);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    void end(){
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);
    }

    // quad with custom UVs
    void quad(float x,float y,float w,float h,float u0=0,float v0=0,float u1=1,float v1=1){
        float verts[6*4]={
            x,   y,   u0,v0,
            x+w, y,   u1,v0,
            x+w, y+h, u1,v1,
            x,   y,   u0,v0,
            x+w, y+h, u1,v1,
            x,   y+h, u0,v1
        };
        glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(verts),verts);
        glDrawArrays(GL_TRIANGLES,0,6);
    }

    void rect(float x,float y,float w,float h,float r,float g,float b,float a){
        glUniform1i(uMode_,0); glUniform4f(uColor_,r,g,b,a);
        quad(x,y,w,h);
    }
    void panel(float x,float y,float w,float h,float radius,float r,float g,float bb,float a){
        glUniform1i(uMode_,2);
        glUniform4f(uColor_,r,g,bb,a);
        glUniform4f(uRect_,x,y,w,h);
        glUniform1f(uRadius_,radius);
        quad(x,y,w,h);
    }
    // bar: filled gradient up to 'fill', empty track behind
    void bar(float x,float y,float w,float h,float fill,
             float r,float g,float b,float a,
             float tr,float tg,float tb,float ta){
        glUniform1i(uMode_,1);
        glUniform4f(uColor_,r,g,b,a);
        glUniform4f(uColor2_,tr,tg,tb,ta);
        glUniform1f(uFill_, fill);
        quad(x,y,w,h);
    }
    void vignette(float r,float g,float b,float a){
        glUniform1i(uMode_,4);
        glUniform4f(uColor_,r,g,b,a);
        quad(0,0,(float)w_,(float)h_);
    }

    float textWidth(const std::string& s,float size){
        return s.size()*size*0.72f;
    }
    // low-level text draw at a given colour (no shadow)
    void textRaw(const std::string& s,float x,float y,float size,
              float r,float g,float b,float a){
        glUniform1i(uMode_,3);
        glUniform4f(uColor_,r,g,b,a);
        glUniform1i(uTex_,0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,atlas_.tex);
        int n=(int)strlen(charSet());
        float adv=size*0.72f;
        float cx=x;
        for(char c: s){
            int idx=charIndex(c);
            float u0=(float)idx/n, u1=(float)(idx+1)/n;
            if(c!=' ') quad(cx,y,size*0.62f,size, u0,0,u1,1);
            cx+=adv;
        }
    }
    // text with a soft drop shadow for legibility over busy scenes
    void text(const std::string& s,float x,float y,float size,
              float r,float g,float b,float a=1.0f){
        float o=(size*0.07f>1.0f)?size*0.07f:1.0f;
        textRaw(s,x+o,y+o,size, 0.0f,0.0f,0.0f, a*0.65f);
        textRaw(s,x,y,size, r,g,b,a);
    }
    void textCentered(const std::string& s,float cx,float y,float size,
                      float r,float g,float b,float a=1.0f){
        text(s, cx-textWidth(s,size)*0.5f, y, size, r,g,b,a);
    }
    int screenW()const{return w_;}
    int screenH()const{return h_;}
private:
    GLuint prog_=0, vao_=0, vbo_=0;
    Atlas atlas_;
    int w_=1280,h_=720;
    GLint uScreen_,uMode_,uColor_,uColor2_,uRect_,uRadius_,uTex_,uFill_,uTime_;
};

} // namespace ui
