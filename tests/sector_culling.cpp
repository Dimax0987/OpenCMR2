#include "port/types.h"
#include "Sector.h"
#include "SceneNode.h"
#include "Graphics.h"
#include "high_address.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include <cstdint>
extern void Scene_InitFixedMathTables();
extern void Sector_CullGridAroundViewNode(SceneNode *, void *);
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr, "culling: failed line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
int main(){
 Scene_InitFixedMathTables();
 // The projection's _11 the culling reads; 0, as here, keeps the original's triangle.
 CGraphics::m_projection11=0;
 const int rows=8,columns=8,count=64,size=10;
 g_sectorsPerRow=columns;g_sectorRows=rows;g_sectorCount=count;g_sectorSize=size*65536;g_sectorHalfSize=size*32768;g_sectorScale=65536/size;
 // Use a large allocation so a non-PIE Linux executable also exercises high
 // native addresses, rather than its low brk heap. The fixture has 64 cells.
 auto *cells=(Sector *)Test_AllocateHighAddress(2*1024*1024);assert(cells);
 if(sizeof(void *)==8)assert((uintptr_t)cells>UINT32_MAX);
 for(int r: {0,3,4,7})for(int c:{0,3,4,7})for(int yaw=0;yaw<8;yaw++)for(int radius:{20,50,100}){
  for(int i=0;i<count;i++){
   Sector &s=cells[i];memset(&s,0xa5,sizeof(s));s.x=(i%columns)*size*65536;s.y=0;s.z=-(i/columns)*size*65536;
   int loX=s.x-g_sectorHalfSize,hiX=s.x+g_sectorHalfSize,loZ=s.z-g_sectorHalfSize,hiZ=s.z+g_sectorHalfSize;
   int rect[4][2]={{loX,loZ},{hiX,loZ},{hiX,hiZ},{loX,hiZ}};memcpy(s.bounds,rect,sizeof(rect));g_sectors[i]=&s;
  }
  SceneNode node={};node.world.position={c*size*65536,0,-r*size*65536};
  double angle=yaw*3.141592653589793/4;node.world.forward={int(llround(sin(angle)*65536)),0,int(llround(cos(angle)*65536))};
  CGraphics::m_farPlaneFixed=radius*65536;
  Sector original[count];memcpy(original,cells,sizeof(original));
  unsigned short previous[count];int previousCount=-1;
  for(int pass=0;pass<3;pass++){
   Sector_CullGridAroundViewNode(&node,NULL);
   if(pass){assert(previousCount==g_sectorCullEnabled);assert(!memcmp(previous,g_visibleSectorIndices,previousCount*sizeof(short)));}
   previousCount=g_sectorCullEnabled;memcpy(previous,g_visibleSectorIndices,previousCount*sizeof(short));
   bool marked[count]={};for(int j=0;j<g_sectorCullEnabled;j++){int id=g_visibleSectorIndices[j];assert(id>=0&&id<count&&!marked[id]);marked[id]=true;}
   for(int i=0;i<count;i++){
    assert(!memcmp(original[i].bounds,cells[i].bounds,sizeof(cells[i].bounds)));
    Sector expected=original[i];if(marked[i]){
     int dx=expected.x-node.world.position.x,dz=expected.z-node.world.position.z;
     expected.viewDistanceSquared=int((int64_t(dx)*dx>>16)+(int64_t(dz)*dz>>16));expected.field_0x80=0;expected.field_0x84=1;
    }
    assert(!memcmp(&expected,&cells[i],sizeof(expected)));assert(bool(g_sectorVisibleBits[i>>5]&(1u<<(i&31)))==marked[i]);
   }
  }
 }
 // A view wider than 90 degrees (21:9, projection _11 about 0.66, a half-angle
 // of 56.6 degrees) keeps what the original's triangle keeps and adds the
 // sectors past its 45-degree sides: with 40 m sectors, from the camera at
 // column 3, row 3, looking along +z (to lower rows), the sector at column 7,
 // row 1 is 160 across and 80 ahead; its nearest corner, 140 across and 100
 // ahead, is outside 45 degrees and inside 56.6, and so is the nearest corner
 // of the 16 m margin around it, 124 across and 116 ahead.
 const int wide=40;
 g_sectorSize=wide*65536;g_sectorHalfSize=wide*32768;g_sectorScale=65536/wide;
 for(int i=0;i<count;i++){
  Sector &s=cells[i];memset(&s,0,sizeof(s));s.x=(i%columns)*wide*65536;s.y=0;s.z=-(i/columns)*wide*65536;
  int loX=s.x-g_sectorHalfSize,hiX=s.x+g_sectorHalfSize,loZ=s.z-g_sectorHalfSize,hiZ=s.z+g_sectorHalfSize;
  int rect[4][2]={{loX,loZ},{hiX,loZ},{hiX,hiZ},{loX,hiZ}};memcpy(s.bounds,rect,sizeof(rect));g_sectors[i]=&s;
 }
 SceneNode node={};node.world.position={3*wide*65536,0,-3*wide*65536};node.world.forward={0,0,65536};
 CGraphics::m_farPlaneFixed=200*65536;
 const int edge=1*columns+7;
 auto visible=[&](float projection,bool *out){
  CGraphics::m_projection11=projection;Sector_CullGridAroundViewNode(&node,NULL);
  for(int i=0;i<count;i++)out[i]=g_sectorVisibleBits[i>>5]&(1u<<(i&31));
 };
 bool at90[count],at21x9[count],mirrored[count];
 visible(1.0f,at90);visible(0.66f,at21x9);visible(-0.66f,mirrored);
 assert(!at90[edge]&&at21x9[edge]);
 for(int i=0;i<count;i++){assert(!at90[i]||at21x9[i]);assert(mirrored[i]==at21x9[i]);}
 // Every sector the view triangle touches is kept, also those it crosses
 // without holding a corner (at its far corners) and those past the walk of
 // the original's half-width square (a view diagonal to the grid). The
 // reference is a separating axis test in doubles, with a small margin for the
 // fixed point rounding.
 g_sectorSize=size*65536;g_sectorHalfSize=size*32768;g_sectorScale=65536/size;
 for(int i=0;i<count;i++){
  Sector &s=cells[i];memset(&s,0,sizeof(s));s.x=(i%columns)*size*65536;s.y=0;s.z=-(i/columns)*size*65536;
  int loX=s.x-g_sectorHalfSize,hiX=s.x+g_sectorHalfSize,loZ=s.z-g_sectorHalfSize,hiZ=s.z+g_sectorHalfSize;
  int rect[4][2]={{loX,loZ},{hiX,loZ},{hiX,hiZ},{loX,hiZ}};memcpy(s.bounds,rect,sizeof(rect));g_sectors[i]=&s;
 }
 int touched=0,cornerless=0;
 for(float projection:{0.0f,0.66f})for(int r:{2,5})for(int c:{2,5})for(int yaw=0;yaw<16;yaw++)for(int radius:{25,40,60}){
  SceneNode view={};view.world.position={c*size*65536,0,-r*size*65536};
  double angle=yaw*3.141592653589793/8;double f[2]={sin(angle),cos(angle)};
  view.world.forward={int(llround(f[0]*65536)),0,int(llround(f[1]*65536))};
  CGraphics::m_farPlaneFixed=radius*65536;CGraphics::m_projection11=projection;
  Sector_CullGridAroundViewNode(&view,NULL);
  double half=projection>0?radius/projection:radius,px=c*size,pz=-r*size;
  double T[3][2]={{px,pz},{px+f[0]*radius-f[1]*half,pz+f[1]*radius+f[0]*half},{px+f[0]*radius+f[1]*half,pz+f[1]*radius-f[0]*half}};
  for(int i=0;i<count;i++){
   double R[4][2];for(int k=0;k<4;k++){R[k][0]=cells[i].bounds[k][0]/65536.0;R[k][1]=cells[i].bounds[k][1]/65536.0;}
   bool overlap=true;
   for(int shape=0;shape<2&&overlap;shape++){int n=shape?3:4;
    for(int e=0;e<n&&overlap;e++){
     const double *a=shape?T[e]:R[e],*b=shape?T[(e+1)%3]:R[(e+1)%4];
     double ax=a[1]-b[1],az=b[0]-a[0],l=sqrt(ax*ax+az*az);ax/=l;az/=l;
     double lo[2]={1e9,1e9},hi[2]={-1e9,-1e9};
     for(int k=0;k<4;k++){double d=R[k][0]*ax+R[k][1]*az;lo[0]=fmin(lo[0],d);hi[0]=fmax(hi[0],d);}
     for(int k=0;k<3;k++){double d=T[k][0]*ax+T[k][1]*az;lo[1]=fmin(lo[1],d);hi[1]=fmax(hi[1],d);}
     if(hi[0]<lo[1]+0.01||hi[1]<lo[0]+0.01)overlap=false;
    }
   }
   if(!overlap)continue;
   touched++;assert(g_sectorVisibleBits[i>>5]&(1u<<(i&31)));
   auto inside=[&](double x,double z){double s0=(T[1][0]-T[0][0])*(z-T[0][1])-(T[1][1]-T[0][1])*(x-T[0][0]),s1=(T[2][0]-T[1][0])*(z-T[1][1])-(T[2][1]-T[1][1])*(x-T[1][0]),s2=(T[0][0]-T[2][0])*(z-T[2][1])-(T[0][1]-T[2][1])*(x-T[2][0]);return (s0>=0&&s1>=0&&s2>=0)||(s0<=0&&s1<=0&&s2<=0);};
   bool corner=false;for(int k=0;k<4;k++)corner=corner||inside(R[k][0],R[k][1]);
   int dr=abs(i/columns-r),dc=abs(i%columns-c);
   if(!corner&&(dr>1||dc>1))cornerless++;
  }
 }
 assert(touched>0&&cornerless>0);
 Test_FreeHighAddress(cells);printf("PASS 384 native culling cases, three passes each: stable bounds, full records, masks and sector lists; a 21:9 view keeps the 90-degree view's sectors and adds those past its sides; %d sectors the view triangle touches are kept, %d of them without a corner inside it\n",touched,cornerless);
}
