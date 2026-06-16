#pragma once
// =============================================================
//  BACKROOMS map / world.
//  A large procedurally-grown maze of yellow rooms following
//  Backrooms Level-0 design principles: big open rooms, long
//  corridors, dead ends, hidden alcoves, plus tagged special
//  zones (maintenance, storage, service corridors) that swap in
//  different materials for variety.
//
//  Grid legend:
//    '#'  yellow wallpaper wall (default)
//    '.'  open carpet room
//    'C'  open CONCRETE service-area floor (zone tag)
//    'T'  open TILE maintenance floor (zone tag)
//    'X'  CONCRETE wall (service/utility wall)
//
//  Collision uses solid per-cell AABBs with wall thickness, so
//  the player slides cleanly and never tunnels through thin geo.
// =============================================================
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace bmap {

constexpr float CELL   = 4.0f;   // size of each grid cell (world units)
constexpr float WALL_H = 3.4f;   // wall / ceiling height
constexpr int   GW     = 49;     // grid width  (odd for maze growth)
constexpr int   GH     = 49;     // grid height

// -------------------------------------------------------------
//  Dynamic obstacle registry. Props, pillars, cameras and other
//  solid volumes register an axis-aligned footprint here so the
//  player physically collides with them (no clipping through).
// -------------------------------------------------------------
struct Obstacle { float minx,maxx,minz,maxz; float top; };
inline std::vector<Obstacle>& obstacles(){ static std::vector<Obstacle> o; return o; }
inline void clearObstacles(){ obstacles().clear(); }
inline void addObstacle(float cx,float cz,float halfX,float halfZ,float top=2.0f){
    obstacles().push_back({cx-halfX,cx+halfX,cz-halfZ,cz+halfZ,top});
}

// runtime grid (filled by generate())
inline std::vector<std::string>& grid(){ static std::vector<std::string> g; return g; }

// deterministic RNG so layout is stable across runs/builds
struct Rng { uint32_t s; Rng(uint32_t x):s(x?x:1){}
    uint32_t next(){ s^=s<<13; s^=s>>17; s^=s<<5; return s; }
    int range(int n){ return (int)(next()%(uint32_t)n); }
    float f(){ return (next()&0xffffff)/16777215.0f; }
};

inline int rows(){ return (int)grid().size(); }
inline int cols(){ return grid().empty()?0:(int)grid()[0].size(); }

inline char cellChar(int r,int c){
    if(r<0||c<0||r>=rows()||c>=cols()) return '#';
    return grid()[r][c];
}
inline bool isWall(int r,int c){
    char ch=cellChar(r,c);
    return ch=='#' || ch=='X';
}
inline bool isConcreteWall(int r,int c){ return cellChar(r,c)=='X'; }

// floor zone of an open cell: 0=carpet 1=concrete 2=tile
inline int floorZone(int r,int c){
    char ch=cellChar(r,c);
    if(ch=='C') return 1;
    if(ch=='T') return 2;
    return 0;
}

inline glm::vec3 cellCenter(int r,int c){
    return glm::vec3(c*CELL+CELL*0.5f, 0, r*CELL+CELL*0.5f);
}
inline float worldW(){ return cols()*CELL; }
inline float worldD(){ return rows()*CELL; }

// -------------------------------------------------------------
//  Generate the world. Recursive-backtracker maze on odd cells,
//  then carve large rooms, knock extra openings (loops), add
//  dead-end alcoves and tag special-material zones.
// -------------------------------------------------------------
inline void generate(uint32_t seed=1337){
    auto& g=grid();
    g.assign(GH, std::string(GW,'#'));
    Rng rng(seed);

    // 1) maze on odd cells via recursive backtracker
    std::vector<std::pair<int,int>> stack;
    int sr=1, sc=1;
    g[sr][sc]='.';
    stack.push_back({sr,sc});
    while(!stack.empty()){
        auto [r,c]=stack.back();
        int dirs[4]={0,1,2,3};
        for(int i=3;i>0;i--){ int j=rng.range(i+1); std::swap(dirs[i],dirs[j]); }
        bool moved=false;
        for(int d=0; d<4; d++){
            int dr=0,dc=0;
            switch(dirs[d]){case 0:dr=-2;break;case 1:dr=2;break;case 2:dc=-2;break;case 3:dc=2;break;}
            int nr=r+dr, nc=c+dc;
            if(nr>0&&nr<GH-1&&nc>0&&nc<GW-1&&g[nr][nc]=='#'){
                g[r+dr/2][c+dc/2]='.';
                g[nr][nc]='.';
                stack.push_back({nr,nc});
                moved=true; break;
            }
        }
        if(!moved) stack.pop_back();
    }

    // 2) carve several big open rooms (signature endless yellow rooms)
    int nRooms=10;
    for(int k=0;k<nRooms;k++){
        int rw=4+rng.range(6), rh=4+rng.range(6);
        int r0=1+rng.range(GH-rh-2), c0=1+rng.range(GW-rw-2);
        for(int r=r0;r<r0+rh;r++) for(int c=c0;c<c0+rw;c++)
            if(r>0&&c>0&&r<GH-1&&c<GW-1) g[r][c]='.';
    }

    // 3) loops: randomly remove some interior walls so it isn't a
    //    perfect tree (creates the disorienting interconnected feel)
    for(int r=1;r<GH-1;r++) for(int c=1;c<GW-1;c++){
        if(g[r][c]=='#'){
            bool h = (g[r][c-1]!='#'&&g[r][c+1]!='#');
            bool v = (g[r-1][c]!='#'&&g[r+1][c]!='#');
            if((h||v) && rng.f()<0.10f) g[r][c]='.';
        }
    }

    // 4) special material zones: pick a few rooms and retag them.
    //    Concrete service block + tile maintenance block.
    auto tagBlock=[&](int r0,int c0,int rw,int rh,char floorCh,char wallCh){
        for(int r=r0-1;r<=r0+rh;r++) for(int c=c0-1;c<=c0+rw;c++){
            if(r<=0||c<=0||r>=GH-1||c>=GW-1) continue;
            if(r==r0-1||r==r0+rh||c==c0-1||c==c0+rw){
                if(g[r][c]=='#') g[r][c]=wallCh; // only convert solid walls to themed walls
            } else {
                if(g[r][c]!='#') g[r][c]=floorCh; // retag open floor
            }
        }
    };
    // a couple of concrete service rooms
    for(int k=0;k<3;k++){
        int rw=3+rng.range(4), rh=3+rng.range(4);
        int r0=2+rng.range(GH-rh-4), c0=2+rng.range(GW-rw-4);
        for(int r=r0;r<r0+rh;r++) for(int c=c0;c<c0+rw;c++) if(r<GH-1&&c<GW-1) g[r][c]='C';
        tagBlock(r0,c0,rw,rh,'C','X');
    }
    // tile maintenance rooms
    for(int k=0;k<2;k++){
        int rw=2+rng.range(3), rh=2+rng.range(3);
        int r0=2+rng.range(GH-rh-4), c0=2+rng.range(GW-rw-4);
        for(int r=r0;r<r0+rh;r++) for(int c=c0;c<c0+rw;c++) if(r<GH-1&&c<GW-1) g[r][c]='T';
        tagBlock(r0,c0,rw,rh,'T','X');
    }

    // 5) ensure a roomy spawn area near (1,1)
    for(int r=1;r<=4;r++) for(int c=1;c<=6;c++) g[r][c]='.';
    for(int c=1;c<=20;c++) g[2][c]='.'; // a long spawn corridor for orientation
}

// -------------------------------------------------------------
//  Collision: solid cell AABBs with thickness. Returns true if a
//  circle of 'radius' at (x,z) overlaps any wall cell.
// -------------------------------------------------------------
inline bool blocked(float x,float z,float radius){
    // outer boundary: never allow leaving the grid footprint
    if(x<CELL*0.5f || z<CELL*0.5f || x>worldW()-CELL*0.5f || z>worldD()-CELL*0.5f) return true;
    int c0=(int)floorf((x-radius)/CELL), c1=(int)floorf((x+radius)/CELL);
    int r0=(int)floorf((z-radius)/CELL), r1=(int)floorf((z+radius)/CELL);
    for(int r=r0;r<=r1;r++) for(int c=c0;c<=c1;c++){
        if(isWall(r,c)){
            // AABB of this wall cell
            float minx=c*CELL, maxx=minx+CELL;
            float minz=r*CELL, maxz=minz+CELL;
            float nx=std::fmax(minx, std::fmin(x,maxx));
            float nz=std::fmax(minz, std::fmin(z,maxz));
            float dx=x-nx, dz=z-nz;
            if(dx*dx+dz*dz < radius*radius) return true;
        }
    }
    // dynamic obstacles (props, pillars, cameras)
    for(const auto& o: obstacles()){
        float nx=std::fmax(o.minx, std::fmin(x,o.maxx));
        float nz=std::fmax(o.minz, std::fmin(z,o.maxz));
        float dx=x-nx, dz=z-nz;
        if(dx*dx+dz*dz < radius*radius) return true;
    }
    return false;
}

} // namespace bmap
