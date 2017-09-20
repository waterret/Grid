 /*************************************************************************************
    Grid physics library, www.github.com/paboyle/Grid 
    Source file: ./lib/lattice/Lattice_reduction.h
    Copyright (C) 2015
Author: Azusa Yamaguchi <ayamaguc@staffmail.ed.ac.uk>
Author: Peter Boyle <paboyle@ph.ed.ac.uk>
Author: paboyle <paboyle@ph.ed.ac.uk>
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    See the full license in the file "LICENSE" in the top level distribution directory
    *************************************************************************************/
    /*  END LEGAL */
#ifndef GRID_LATTICE_REDUCTION_H
#define GRID_LATTICE_REDUCTION_H

#include <Grid/Grid_Eigen_Dense.h>

namespace Grid {
#ifdef GRID_WARN_SUBOPTIMAL
#warning "Optimisation alert all these reduction loops are NOT threaded "
#endif     

  ////////////////////////////////////////////////////////////////////////////////////////////////////
  // Deterministic Reduction operations
  ////////////////////////////////////////////////////////////////////////////////////////////////////
template<class vobj> inline RealD norm2(const Lattice<vobj> &arg){
  ComplexD nrm = innerProduct(arg,arg);
  return std::real(nrm); 
}

// Double inner product
template<class vobj>
inline ComplexD innerProduct(const Lattice<vobj> &left,const Lattice<vobj> &right) 
{
  typedef typename vobj::scalar_type scalar_type;
  typedef typename vobj::vector_typeD vector_type;
  scalar_type  nrm;
  
  GridBase *grid = left._grid;
  
  std::vector<vector_type,alignedAllocator<vector_type> > sumarray(grid->SumArraySize());
  
  parallel_for(int thr=0;thr<grid->SumArraySize();thr++){
    int nwork, mywork, myoff;
    GridThread::GetWork(left._grid->oSites(),thr,mywork,myoff);
    
    decltype(innerProductD(left._odata[0],right._odata[0])) vnrm=zero; // private to thread; sub summation
    for(int ss=myoff;ss<mywork+myoff; ss++){
      vnrm = vnrm + innerProductD(left._odata[ss],right._odata[ss]);
    }
    sumarray[thr]=TensorRemove(vnrm) ;
  }
  
  vector_type vvnrm; vvnrm=zero;  // sum across threads
  for(int i=0;i<grid->SumArraySize();i++){
    vvnrm = vvnrm+sumarray[i];
  } 
  nrm = Reduce(vvnrm);// sum across simd
  right._grid->GlobalSum(nrm);
  return nrm;
}
 
template<class Op,class T1>
inline auto sum(const LatticeUnaryExpression<Op,T1> & expr)
  ->typename decltype(expr.first.func(eval(0,std::get<0>(expr.second))))::scalar_object
{
  return sum(closure(expr));
}

template<class Op,class T1,class T2>
inline auto sum(const LatticeBinaryExpression<Op,T1,T2> & expr)
      ->typename decltype(expr.first.func(eval(0,std::get<0>(expr.second)),eval(0,std::get<1>(expr.second))))::scalar_object
{
  return sum(closure(expr));
}


template<class Op,class T1,class T2,class T3>
inline auto sum(const LatticeTrinaryExpression<Op,T1,T2,T3> & expr)
  ->typename decltype(expr.first.func(eval(0,std::get<0>(expr.second)),
				      eval(0,std::get<1>(expr.second)),
				      eval(0,std::get<2>(expr.second))
				      ))::scalar_object
{
  return sum(closure(expr));
}

template<class vobj>
inline typename vobj::scalar_object sum(const Lattice<vobj> &arg)
{
  GridBase *grid=arg._grid;
  int Nsimd = grid->Nsimd();
  
  std::vector<vobj,alignedAllocator<vobj> > sumarray(grid->SumArraySize());
  for(int i=0;i<grid->SumArraySize();i++){
    sumarray[i]=zero;
  }
  
  parallel_for(int thr=0;thr<grid->SumArraySize();thr++){
    int nwork, mywork, myoff;
    GridThread::GetWork(grid->oSites(),thr,mywork,myoff);
    
    vobj vvsum=zero;
    for(int ss=myoff;ss<mywork+myoff; ss++){
      vvsum = vvsum + arg._odata[ss];
    }
    sumarray[thr]=vvsum;
  }
  
  vobj vsum=zero;  // sum across threads
  for(int i=0;i<grid->SumArraySize();i++){
    vsum = vsum+sumarray[i];
  } 
  
  typedef typename vobj::scalar_object sobj;
  sobj ssum=zero;
  
  std::vector<sobj>               buf(Nsimd);
  extract(vsum,buf);
  
  for(int i=0;i<Nsimd;i++) ssum = ssum + buf[i];
  arg._grid->GlobalSum(ssum);
  
  return ssum;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sliceSum, sliceInnerProduct, sliceAxpy, sliceNorm etc...
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<class vobj> inline void sliceSumNodeLocal(const Lattice<vobj> &Data,std::vector<typename vobj::scalar_object> &result,int orthogdim)
{
  ///////////////////////////////////////////////////////
  // FIXME precision promoted summation
  // may be important for correlation functions
  // But easily avoided by using double precision fields
  ///////////////////////////////////////////////////////
  typedef typename vobj::scalar_object sobj;
  GridBase  *grid = Data._grid;
  assert(grid!=NULL);

  const int    Nd = grid->_ndimension;
  const int Nsimd = grid->Nsimd();

  assert(orthogdim >= 0);
  assert(orthogdim < Nd);

  int fd=grid->_fdimensions[orthogdim];
  int ld=grid->_ldimensions[orthogdim];
  int rd=grid->_rdimensions[orthogdim];

  std::vector<vobj,alignedAllocator<vobj> > lvSum(rd); // will locally sum vectors first
  std::vector<sobj> lsSum(ld,zero);                    // sum across these down to scalars
  std::vector<sobj> extracted(Nsimd);                  // splitting the SIMD

  result.resize(fd); // And then global sum to return the same vector to every node 
  for(int r=0;r<rd;r++){
    lvSum[r]=zero;
  }

  int e1=    grid->_slice_nblock[orthogdim];
  int e2=    grid->_slice_block [orthogdim];
  int stride=grid->_slice_stride[orthogdim];

  // sum over reduced dimension planes, breaking out orthog dir
  // Parallel over orthog direction
  parallel_for(int r=0;r<rd;r++){

    int so=r*grid->_ostride[orthogdim]; // base offset for start of plane 

    for(int n=0;n<e1;n++){
      for(int b=0;b<e2;b++){
	int ss= so+n*stride+b;
	lvSum[r]=lvSum[r]+Data._odata[ss];
      }
    }
  }

  // Sum across simd lanes in the plane, breaking out orthog dir.
  std::vector<int> icoor(Nd);

  for(int rt=0;rt<rd;rt++){

    extract(lvSum[rt],extracted);

    for(int idx=0;idx<Nsimd;idx++){

      grid->iCoorFromIindex(icoor,idx);

      int ldx =rt+icoor[orthogdim]*rd;

      lsSum[ldx]=lsSum[ldx]+extracted[idx];

    }
  }
  
  // sum over nodes.
  sobj gsum;
  for(int t=0;t<fd;t++){
    int pt = t/ld; // processor plane
    int lt = t%ld;
    if ( pt == grid->_processor_coor[orthogdim] ) {
      gsum=lsSum[lt];
    } else {
      gsum=zero;
    }

    //grid->GlobalSum(gsum);

    result[t]=gsum;
  }
}


template<class vobj> inline void sliceSum(const Lattice<vobj> &Data,std::vector<typename vobj::scalar_object> &result,int orthogdim)
{
  GridBase  *grid = Data._grid;
  assert(grid!=NULL);
  sliceSumNodeLocal(Data,result,orthogdim);
  grid->GlobalSum(result);
}

template<class vobj>
static void sliceInnerProductVector( std::vector<ComplexD> & result, const Lattice<vobj> &lhs,const Lattice<vobj> &rhs,int orthogdim) 
{
  typedef typename vobj::vector_type   vector_type;
  typedef typename vobj::scalar_type   scalar_type;
  GridBase  *grid = lhs._grid;
  assert(grid!=NULL);
  conformable(grid,rhs._grid);

  const int    Nd = grid->_ndimension;
  const int Nsimd = grid->Nsimd();

  assert(orthogdim >= 0);
  assert(orthogdim < Nd);

  int fd=grid->_fdimensions[orthogdim];
  int ld=grid->_ldimensions[orthogdim];
  int rd=grid->_rdimensions[orthogdim];

  std::vector<vector_type,alignedAllocator<vector_type> > lvSum(rd); // will locally sum vectors first
  std::vector<scalar_type > lsSum(ld,scalar_type(0.0));                    // sum across these down to scalars
  std::vector<iScalar<scalar_type> > extracted(Nsimd);                  // splitting the SIMD

  result.resize(fd); // And then global sum to return the same vector to every node for IO to file
  for(int r=0;r<rd;r++){
    lvSum[r]=zero;
  }

  int e1=    grid->_slice_nblock[orthogdim];
  int e2=    grid->_slice_block [orthogdim];
  int stride=grid->_slice_stride[orthogdim];

  parallel_for(int r=0;r<rd;r++){

    int so=r*grid->_ostride[orthogdim]; // base offset for start of plane 

    for(int n=0;n<e1;n++){
      for(int b=0;b<e2;b++){
	int ss= so+n*stride+b;
	vector_type vv = TensorRemove(innerProduct(lhs._odata[ss],rhs._odata[ss]));
	lvSum[r]=lvSum[r]+vv;
      }
    }
  }

  // Sum across simd lanes in the plane, breaking out orthog dir.
  std::vector<int> icoor(Nd);
  for(int rt=0;rt<rd;rt++){

    iScalar<vector_type> temp; 
    temp._internal = lvSum[rt];
    extract(temp,extracted);

    for(int idx=0;idx<Nsimd;idx++){

      grid->iCoorFromIindex(icoor,idx);

      int ldx =rt+icoor[orthogdim]*rd;

      lsSum[ldx]=lsSum[ldx]+extracted[idx]._internal;

    }
  }
  
  // sum over nodes.
  scalar_type gsum;
  for(int t=0;t<fd;t++){
    int pt = t/ld; // processor plane
    int lt = t%ld;
    if ( pt == grid->_processor_coor[orthogdim] ) {
      gsum=lsSum[lt];
    } else {
      gsum=scalar_type(0.0);
    }

    grid->GlobalSum(gsum);

    result[t]=gsum;
  }
}
template<class vobj>
static void sliceNorm (std::vector<RealD> &sn,const Lattice<vobj> &rhs,int Orthog) 
{
  typedef typename vobj::scalar_object sobj;
  typedef typename vobj::scalar_type scalar_type;
  typedef typename vobj::vector_type vector_type;
  
  int Nblock = rhs._grid->GlobalDimensions()[Orthog];
  std::vector<ComplexD> ip(Nblock);
  sn.resize(Nblock);
  
  sliceInnerProductVector(ip,rhs,rhs,Orthog);
  for(int ss=0;ss<Nblock;ss++){
    sn[ss] = real(ip[ss]);
  }
};


template<class vobj>
static void sliceMaddVector(Lattice<vobj> &R,std::vector<RealD> &a,const Lattice<vobj> &X,const Lattice<vobj> &Y,
			    int orthogdim,RealD scale=1.0) 
{    
  typedef typename vobj::scalar_object sobj;
  typedef typename vobj::scalar_type scalar_type;
  typedef typename vobj::vector_type vector_type;
  typedef typename vobj::tensor_reduced tensor_reduced;
  
  GridBase *grid  = X._grid;

  int Nsimd  =grid->Nsimd();
  int Nblock =grid->GlobalDimensions()[orthogdim];

  int fd     =grid->_fdimensions[orthogdim];
  int ld     =grid->_ldimensions[orthogdim];
  int rd     =grid->_rdimensions[orthogdim];

  int e1     =grid->_slice_nblock[orthogdim];
  int e2     =grid->_slice_block [orthogdim];
  int stride =grid->_slice_stride[orthogdim];

  std::vector<int> icoor;

  for(int r=0;r<rd;r++){

    int so=r*grid->_ostride[orthogdim]; // base offset for start of plane 

    vector_type    av;

    for(int l=0;l<Nsimd;l++){
      grid->iCoorFromIindex(icoor,l);
      int ldx =r+icoor[orthogdim]*rd;
      scalar_type *as =(scalar_type *)&av;
      as[l] = scalar_type(a[ldx])*scale;
    }

    tensor_reduced at; at=av;

    parallel_for_nest2(int n=0;n<e1;n++){
      for(int b=0;b<e2;b++){
	int ss= so+n*stride+b;
	R._odata[ss] = at*X._odata[ss]+Y._odata[ss];
      }
    }
  }
};


/*
template<class vobj>
static void sliceMaddVectorSlow (Lattice<vobj> &R,std::vector<RealD> &a,const Lattice<vobj> &X,const Lattice<vobj> &Y,
			     int Orthog,RealD scale=1.0) 
{    
  // FIXME: Implementation is slow
  // Best base the linear combination by constructing a 
  // set of vectors of size grid->_rdimensions[Orthog].
  typedef typename vobj::scalar_object sobj;
  typedef typename vobj::scalar_type scalar_type;
  typedef typename vobj::vector_type vector_type;
  
  int Nblock = X._grid->GlobalDimensions()[Orthog];
  
  GridBase *FullGrid  = X._grid;
  GridBase *SliceGrid = makeSubSliceGrid(FullGrid,Orthog);
  
  Lattice<vobj> Xslice(SliceGrid);
  Lattice<vobj> Rslice(SliceGrid);
  // If we based this on Cshift it would work for spread out
  // but it would be even slower
  for(int i=0;i<Nblock;i++){
    ExtractSlice(Rslice,Y,i,Orthog);
    ExtractSlice(Xslice,X,i,Orthog);
    Rslice = Rslice + Xslice*(scale*a[i]);
    InsertSlice(Rslice,R,i,Orthog);
  }
};
template<class vobj>
static void sliceInnerProductVectorSlow( std::vector<ComplexD> & vec, const Lattice<vobj> &lhs,const Lattice<vobj> &rhs,int Orthog) 
  {
    // FIXME: Implementation is slow
    // Look at localInnerProduct implementation,
    // and do inside a site loop with block strided iterators
    typedef typename vobj::scalar_object sobj;
    typedef typename vobj::scalar_type scalar_type;
    typedef typename vobj::vector_type vector_type;
    typedef typename vobj::tensor_reduced scalar;
    typedef typename scalar::scalar_object  scomplex;
  
    int Nblock = lhs._grid->GlobalDimensions()[Orthog];
    vec.resize(Nblock);
    std::vector<scomplex> sip(Nblock);
    Lattice<scalar> IP(lhs._grid); 
    IP=localInnerProduct(lhs,rhs);
    sliceSum(IP,sip,Orthog);
  
    for(int ss=0;ss<Nblock;ss++){
      vec[ss] = TensorRemove(sip[ss]);
    }
  }
*/

//////////////////////////////////////////////////////////////////////////////////////////
// FIXME: Implementation is slow
// If we based this on Cshift it would work for spread out
// but it would be even slower
//
// Repeated extract slice is inefficient
//
// Best base the linear combination by constructing a 
// set of vectors of size grid->_rdimensions[Orthog].
//////////////////////////////////////////////////////////////////////////////////////////

inline GridBase         *makeSubSliceGrid(const GridBase *BlockSolverGrid,int Orthog)
{
  int NN    = BlockSolverGrid->_ndimension;
  int nsimd = BlockSolverGrid->Nsimd();
  
  std::vector<int> latt_phys(0);
  std::vector<int> simd_phys(0);
  std::vector<int>  mpi_phys(0);
  
  for(int d=0;d<NN;d++){
    if( d!=Orthog ) { 
      latt_phys.push_back(BlockSolverGrid->_fdimensions[d]);
      simd_phys.push_back(BlockSolverGrid->_simd_layout[d]);
      mpi_phys.push_back(BlockSolverGrid->_processors[d]);
    }
  }
  return (GridBase *)new GridCartesian(latt_phys,simd_phys,mpi_phys); 
}


template<class vobj>
static void sliceMaddMatrix (Lattice<vobj> &R,Eigen::MatrixXcd &aa,const Lattice<vobj> &X,const Lattice<vobj> &Y,int Orthog,RealD scale=1.0) 
{    
  typedef typename vobj::scalar_object sobj;
  typedef typename vobj::scalar_type scalar_type;
  typedef typename vobj::vector_type vector_type;

  int Nblock = X._grid->GlobalDimensions()[Orthog];
  
  GridBase *FullGrid  = X._grid;
  GridBase *SliceGrid = makeSubSliceGrid(FullGrid,Orthog);
  
  Lattice<vobj> Xslice(SliceGrid);
  Lattice<vobj> Rslice(SliceGrid);
  
  for(int i=0;i<Nblock;i++){
    ExtractSlice(Rslice,Y,i,Orthog);
    for(int j=0;j<Nblock;j++){
      ExtractSlice(Xslice,X,j,Orthog);
      Rslice = Rslice + Xslice*(scale*aa(j,i));
    }
    InsertSlice(Rslice,R,i,Orthog);
  }
};

template<class vobj>
static void sliceInnerProductMatrix(  Eigen::MatrixXcd &mat, const Lattice<vobj> &lhs,const Lattice<vobj> &rhs,int Orthog) 
{
  // FIXME: Implementation is slow
  // Not sure of best solution.. think about it
  typedef typename vobj::scalar_object sobj;
  typedef typename vobj::scalar_type scalar_type;
  typedef typename vobj::vector_type vector_type;
  
  GridBase *FullGrid  = lhs._grid;
  GridBase *SliceGrid = makeSubSliceGrid(FullGrid,Orthog);
  
  int Nblock = FullGrid->GlobalDimensions()[Orthog];
  
  Lattice<vobj> Lslice(SliceGrid);
  Lattice<vobj> Rslice(SliceGrid);
  
  mat = Eigen::MatrixXcd::Zero(Nblock,Nblock);
  
  for(int i=0;i<Nblock;i++){
    ExtractSlice(Lslice,lhs,i,Orthog);
    for(int j=0;j<Nblock;j++){
      ExtractSlice(Rslice,rhs,j,Orthog);
      mat(i,j) = innerProduct(Lslice,Rslice);
    }
  }
#undef FORCE_DIAG
#ifdef FORCE_DIAG
  for(int i=0;i<Nblock;i++){
    for(int j=0;j<Nblock;j++){
      if ( i != j ) mat(i,j)=0.0;
    }
  }
#endif
  return;
}

} /*END NAMESPACE GRID*/
#endif



