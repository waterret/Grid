
#include <Grid.h>

namespace Grid {
namespace QCD {

const std::vector<int> WilsonMatrix::directions   ({0,1,2,3, 0, 1, 2, 3,0});
const std::vector<int> WilsonMatrix::displacements({1,1,1,1,-1,-1,-1,-1,0});

  // Should be in header?
const int WilsonMatrix::Xp = 0;
const int WilsonMatrix::Yp = 1;
const int WilsonMatrix::Zp = 2;
const int WilsonMatrix::Tp = 3;
const int WilsonMatrix::Xm = 4;
const int WilsonMatrix::Ym = 5;
const int WilsonMatrix::Zm = 6;
const int WilsonMatrix::Tm = 7;
  //const int WilsonMatrix::X0 = 8;

  class WilsonCompressor {
  public:
    int mu;
    
    void Point(int p) { 
      mu=p;
    };

    vHalfSpinColourVector operator () (const vSpinColourVector &in)
    {
      vHalfSpinColourVector ret;
      switch(mu) {
      case WilsonMatrix::Xp:
	spProjXp(ret,in);
	break;
      case WilsonMatrix::Yp:
	spProjYp(ret,in);
	break;
      case WilsonMatrix::Zp:
	spProjZp(ret,in);
	break;
      case WilsonMatrix::Tp:
	spProjTp(ret,in);
	break;
      case WilsonMatrix::Xm:
	spProjXm(ret,in);
	break;
      case WilsonMatrix::Ym:
	spProjYm(ret,in);
	break;
      case WilsonMatrix::Zm:
	spProjZm(ret,in);
	break;
      case WilsonMatrix::Tm:
	spProjTm(ret,in);
	break;
      default: 
	assert(0);
	break;
      }
      return ret;
    }
  };

  WilsonMatrix::WilsonMatrix(LatticeGaugeField &_Umu,double _mass)
  : Stencil(Umu._grid,npoint,0,directions,displacements),
    mass(_mass),
    Umu(_Umu._grid)
{
  // Allocate the required comms buffer
  grid = _Umu._grid;
  comm_buf.resize(Stencil._unified_buffer_size);
  DoubleStore(Umu,_Umu);
}
      
void WilsonMatrix::DoubleStore(LatticeDoubledGaugeField &Uds,const LatticeGaugeField &Umu)
{
  LatticeColourMatrix U(grid);

  for(int mu=0;mu<Nd;mu++){
    U = peekIndex<LorentzIndex>(Umu,mu);
    pokeIndex<LorentzIndex>(Uds,U,mu);
    U = adj(Cshift(U,mu,-1));
    pokeIndex<LorentzIndex>(Uds,U,mu+4);
  }
}

void WilsonMatrix::multiply(const LatticeFermion &in, LatticeFermion &out)
{
  Dhop(in,out);
  return;
}

void WilsonMatrix::Dhop(const LatticeFermion &in, LatticeFermion &out)
{
  WilsonCompressor compressor;
  Stencil.HaloExchange<vSpinColourVector,vHalfSpinColourVector,WilsonCompressor>(in,comm_buf,compressor);

  for(int ss=0;ss<grid->oSites();ss++){

    int offset,local,perm, ptype;

    vSpinColourVector result;
    vHalfSpinColourVector  chi;    
    vHalfSpinColourVector  tmp;    
    vHalfSpinColourVector Uchi;
    vHalfSpinColourVector *chi_p;

    result=zero;

    // Xp
    offset = Stencil._offsets [Xp][ss];
    local  = Stencil._is_local[Xp][ss];
    perm   = Stencil._permute[Xp][ss];
    ptype  = Stencil._permute_type[Xp];
    chi_p  = &comm_buf[offset];
    if ( local ) {
      chi_p = &chi;
      spProjXp(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Xp)),&(*chi_p)());
    spReconXp(result,Uchi);

    // Yp
    offset = Stencil._offsets [Yp][ss];
    local  = Stencil._is_local[Yp][ss];
    perm   = Stencil._permute[Yp][ss];
    ptype  = Stencil._permute_type[Yp];
    chi_p  = &comm_buf[offset];
    if ( local ) {
      chi_p = &chi;
      spProjYp(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Yp)),&(*chi_p)());
    accumReconYp(result,Uchi);

    // Zp
    offset = Stencil._offsets [Zp][ss];
    local  = Stencil._is_local[Zp][ss];
    perm   = Stencil._permute[Zp][ss];
    ptype  = Stencil._permute_type[Zp];
    chi_p  = &comm_buf[offset];

    if ( local ) {
      chi_p = &chi;
      spProjZp(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Zp)),&(*chi_p)());
    accumReconZp(result,Uchi);

    // Tp
    offset = Stencil._offsets [Tp][ss];
    local  = Stencil._is_local[Tp][ss];
    perm   = Stencil._permute[Tp][ss];
    ptype  = Stencil._permute_type[Tp];
    chi_p  = &comm_buf[offset];

    if ( local ) {
      chi_p = &chi;
      spProjTp(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Tp)),&(*chi_p)());
    accumReconTp(result,Uchi);

    // Xm
    offset = Stencil._offsets [Xm][ss];
    local  = Stencil._is_local[Xm][ss];
    perm   = Stencil._permute[Xm][ss];
    ptype  = Stencil._permute_type[Xm];
    chi_p  = &comm_buf[offset];
    if ( local ) {
      chi_p = &chi;
      spProjXm(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Xm)),&(*chi_p)());
    accumReconXm(result,Uchi);


    // Ym
    offset = Stencil._offsets [Ym][ss];
    local  = Stencil._is_local[Ym][ss];
    perm   = Stencil._permute[Ym][ss];
    ptype  = Stencil._permute_type[Ym];
    chi_p  = &comm_buf[offset];

    if ( local ) {
      chi_p = &chi;
      spProjYm(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Ym)),&(*chi_p)());
    accumReconYm(result,Uchi);

    // Zm
    offset = Stencil._offsets [Zm][ss];
    local  = Stencil._is_local[Zm][ss];
    perm   = Stencil._permute[Zm][ss];
    ptype  = Stencil._permute_type[Zm];
    chi_p  = &comm_buf[offset];

    if ( local ) {
      chi_p = &chi;
      spProjZm(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Zm)),&(*chi_p)());
    accumReconZm(result,Uchi);

    // Tm
    offset = Stencil._offsets [Tm][ss];
    local  = Stencil._is_local[Tm][ss];
    perm   = Stencil._permute[Tm][ss];
    ptype  = Stencil._permute_type[Tm];
    chi_p  = &comm_buf[offset];

    if ( local ) {
      chi_p = &chi;
      spProjTm(chi,in._odata[offset]);
      if ( perm ) {
	permute(tmp,chi,ptype);
	chi_p = &tmp;
      }
    }
    mult(&(Uchi()),&(Umu._odata[ss](Tm)),&(*chi_p)());
    accumReconTm(result,Uchi);


    out._odata[ss] = result;
  }

}
void WilsonMatrix::Dw(const LatticeFermion &in, LatticeFermion &out)
{
  return;
}
void WilsonMatrix::MpcDag   (const LatticeFermion &in, LatticeFermion &out)
{
  return;
}
void WilsonMatrix::Mpc      (const LatticeFermion &in, LatticeFermion &out)
{
  return;
}
void WilsonMatrix::MpcDagMpc(const LatticeFermion &in, LatticeFermion &out)
{
  return;
}
void WilsonMatrix::MDagM    (const LatticeFermion &in, LatticeFermion &out)
{
  return;
}


}}
