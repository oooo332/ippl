

#include "Utility/IpplException.h"
#include "Field/HaloCells.h"

namespace ippl {
    namespace detail {

        template<typename T, unsigned Dim, class Mesh, class Cell>
        BCondBase<T, Dim, Mesh, Cell>::BCondBase(unsigned int face)
        : face_m(face)
        , changePhysical_m(false)
        { }


        template<typename T, unsigned Dim, class Mesh, class Cell>
        inline std::ostream&
        operator<<(std::ostream& os, const BCondBase<T, Dim, Mesh, Cell>& bc)
        {
            bc.write(os);
            return os;
        }

    }

    template<typename T, unsigned Dim, class Mesh, class Cell>
    void ExtrapolateFace<T, Dim, Mesh, Cell>::apply(Field_t& field) 
    {
        //We only support constant extrapolation for the moment, other 
        //higher order extrapolation stuffs need to be added.

        unsigned int face = this->face_m; 
        unsigned d = face / 2;
        if(Ippl::Comm->size() > 1) {
            const Layout_t& layout = field.getLayout(); 
            const auto& lDomains = layout.getHostLocalDomains();
            const auto& domain = layout.getDomain(); 
            int myRank = Ippl::Comm->rank();
            bool isUpper = (face & 1) && 
                           (lDomains[myRank][d].max() == domain[d].max());
            bool isLower = (!(face & 1)) && 
                           (lDomains[myRank][d].min() == domain[d].min());
            
            if((!isUpper) && (!isLower))
                return;
        }

        //If we are here then it is a processor with the face on the physical 
        //boundary or it is the single core case. Then the following code is same
        //irrespective of either it is a single core or multi-core case as the
        //non-periodic BC is local to apply.
        typename Field_t::view_type& view = field.getView();
        const int nghost = field.getNghost();
        using mdrange_type = Kokkos::MDRangePolicy<Kokkos::Rank<2>>;
        int src, dest;
        if(face & 1) {
            src  = view.extent(d) - 2;
            dest = src + 1;
        }
        else {
            src  = 1;
            dest = src - 1;
        }
        switch(d) {
            case 0:
                Kokkos::parallel_for("Assign extrapolate BC X", 
                                        mdrange_type({nghost, nghost},
                                                     {view.extent(1) - nghost,
                                                      view.extent(2) - nghost}),
                                        KOKKOS_CLASS_LAMBDA(const size_t j, 
                                                            const size_t k)
                {
                    view(dest, j, k) =  slope_m * view(src, j, k) + offset_m; 
                });
                    break;

            case 1:
                Kokkos::parallel_for("Assign extrapolate BC Y", 
                                        mdrange_type({nghost, nghost},
                                                     {view.extent(0) - nghost,
                                                      view.extent(2) - nghost}),
                                        KOKKOS_CLASS_LAMBDA(const size_t i, 
                                                            const size_t k)
                {
                    view(i, dest, k) =  slope_m * view(i, src, k) + offset_m; 
                });
                    break;
            case 2:
                Kokkos::parallel_for("Assign extrapolate BC Z", 
                                        mdrange_type({nghost, nghost},
                                                     {view.extent(0) - nghost,
                                                      view.extent(1) - nghost}),
                                        KOKKOS_CLASS_LAMBDA(const size_t i, 
                                                            const size_t j)
                {
                    view(i, j, dest) =  slope_m * view(i, j, src) + offset_m; 
                });
                    break;
            default:
                throw IpplException("ExtrapolateFace::apply", 
                                       "face number wrong");
        }
    }
    
    template<typename T, unsigned Dim, class Mesh, class Cell>
    void ExtrapolateFace<T, Dim, Mesh, Cell>::write(std::ostream& out) const
    {
        out << "Constant Extrapolation Face"
            << ", Face = " << this->face_m;
    }

    template<typename T, unsigned Dim, class Mesh, class Cell>
    void NoBcFace<T, Dim, Mesh, Cell>::write(std::ostream& out) const
    {
        out << "NoBcFace"
            << ", Face = " << this->face_m;
    }


    template<typename T, unsigned Dim, class Mesh, class Cell>
    void ConstantFace<T, Dim, Mesh, Cell>::write(std::ostream& out) const
    {
        out << "ConstantFace"
            << ", Face = " << this->face_m
            << ", Constant = " << this->offset_m;
    }


    template<typename T, unsigned Dim, class Mesh, class Cell>
    void ZeroFace<T, Dim, Mesh, Cell>::write(std::ostream& out) const
    {
        out << "ZeroFace"
            << ", Face = " << this->face_m;
    }


    template<typename T, unsigned Dim, class Mesh, class Cell>
    void PeriodicFace<T, Dim, Mesh, Cell>::write(std::ostream& out) const
    {
        out << "PeriodicFace"
            << ", Face = " << this->face_m;
    }
    
    template<typename T, unsigned Dim, class Mesh, class Cell>
    void PeriodicFace<T, Dim, Mesh, Cell>::findBCNeighbors(Field_t& field)
    {
       //For cell centering only face neighbors are needed 
       unsigned int face = this->face_m; 
       unsigned int d =  face/ 2;
       const int nghost = field.getNghost();
       int myRank = Ippl::Comm->rank();
       const Layout_t& layout = field.getLayout(); 
       const auto& lDomains = layout.getHostLocalDomains();
       const auto& domain = layout.getDomain(); 

       for (size_t i = 0; i < faceNeighbors_m.size(); ++i) {
            faceNeighbors_m[i].clear();
       }

       if(lDomains[myRank][d].length() < domain[d].length()) {
            //Only along this dimension we need communication.

            bool isUpper = (face & 1) && 
                           (lDomains[myRank][d].max() == domain[d].max());
            bool isLower = (!(face & 1)) && 
                           (lDomains[myRank][d].min() == domain[d].min());

            if(isUpper || isLower) {
                
                //this face is  on mesh/physical boundary
                // get my local box
                auto& nd = lDomains[myRank];

                // grow the box by nghost cells in dimension d of face
                auto gnd = nd.grow(nghost, d);

                int offset;
                if(face & 1) {
                    //upper face
                    offset = -domain[d].length();
                }
                else {
                    //lower face
                    offset = domain[d].length();
                }
                //shift by offset
                gnd[d] = gnd[d] + offset;
                
                //Now, we are ready to intersect
                for (int rank = 0; rank < Ippl::Comm->size(); ++rank) {
                    if (rank == myRank) {
                        continue;
                    }
                   
                    if (gnd.touches(lDomains[rank])) {
                        faceNeighbors_m[face].push_back(rank);
                    }
                }
            }
       }
    }

    template<typename T, unsigned Dim, class Mesh, class Cell>
    void PeriodicFace<T, Dim, Mesh, Cell>::apply(Field_t& field)
    {
       unsigned int face = this->face_m; 
       unsigned int d = face / 2;
       typename Field_t::view_type& view = field.getView();
       const Layout_t& layout = field.getLayout(); 
       const int nghost = field.getNghost();
       int myRank = Ippl::Comm->rank();
       const auto& lDomains = layout.getHostLocalDomains();
       const auto& domain = layout.getDomain(); 

       if(lDomains[myRank][d].length() < domain[d].length()) {
            //Only along this dimension we need communication.

            bool isUpper = (face & 1) && 
                           (lDomains[myRank][d].max() == domain[d].max());
            bool isLower = (!(face & 1)) && 
                           (lDomains[myRank][d].min() == domain[d].min());

            if(isUpper || isLower) {
                //this face is  on mesh/physical boundary
                // get my local box
                auto& nd = lDomains[myRank];

                int offset, offsetRecv;
                if(face & 1) {
                    //upper face
                    offset = -domain[d].length();
                    offsetRecv = 1;
                }
                else {
                    //lower face
                    offset = domain[d].length();
                    offsetRecv = -1;
                }
                
                std::vector<MPI_Request> requests(0);
                using archive_type = Communicate::archive_type;
                std::vector<std::unique_ptr<archive_type>> archives(0);
                
                int tag = Ippl::Comm->next_tag(HALO_FACE_TAG, HALO_TAG_CYCLE);
                
                using HaloCells_t = detail::HaloCells<T, Dim>;
                using range_t = typename HaloCells_t::bound_type;
                HaloCells_t& halo = field.getHalo();
                std::vector<range_t> rangeNeighbors;
                rangeNeighbors_m.clear();
                
                for (size_t i = 0; i < faceNeighbors_m[face].size(); ++i) {

                    int rank = faceNeighbors_m[face][i];
                        
                    auto ndNeighbor = lDomains[rank];
                    ndNeighbor[d] = ndNeighbor[d] - offset;
                        
                    NDIndex<Dim> gndNeighbor = ndNeighbor.grow(nghost, d);

                    NDIndex<Dim> overlap = gndNeighbor.intersect(nd);

                    range_t range;

                    for (size_t i = 0; i < Dim; ++i) {
                        range.lo[i] = overlap[i].first() - nd[i].first() 
                                      + nghost;
                        range.hi[i] = overlap[i].last()  - nd[i].first() 
                                      + nghost + 1;
                    }
                    
                    rangeNeighbors.push_back(range);    
                    archives.push_back(std::make_unique<archive_type>());
                    requests.resize(requests.size() + 1);

                    detail::FieldBufferData<T> fdSend;
                        
                    halo.pack(range, view, fdSend);

                    Ippl::Comm->isend(rank, tag, fdSend, *(archives.back()),
                                      requests.back());
                }
                
                for (size_t i = 0; i < faceNeighbors_m[face].size(); ++i) {

                    int rank = faceNeighbors_m[face][i];
                    
                    range_t range = rangeNeighbors[i];

                    range.lo[d] = range.lo[d] + offsetRecv;
                    range.hi[d] = range.hi[d] + offsetRecv;
                        
                    detail::FieldBufferData<T> fdRecv;
                        
                    Kokkos::resize(fdRecv.buffer,
                                   (range.hi[0] - range.lo[0]) *
                                   (range.hi[1] - range.lo[1]) *
                                   (range.hi[2] - range.lo[2]));

                    Ippl::Comm->recv(rank, tag, fdRecv);

                    using assign_t = typename HaloCells_t::assign;
                    halo.template unpack<assign_t>(range, view, fdRecv);
                }
                if (requests.size() > 0) {
                    MPI_Waitall(requests.size(), requests.data(), 
                                MPI_STATUSES_IGNORE);
                    archives.clear();
                }
            }
            //For all other processors do nothing
       }
       else {

            using mdrange_type = Kokkos::MDRangePolicy<Kokkos::Rank<2>>;
            int N = view.extent(d);
            
            switch (d) {
                case 0:
                    Kokkos::parallel_for("Assign periodic field BC X", 
                                          mdrange_type({nghost, nghost},
                                                       {view.extent(1) - nghost,
                                                        view.extent(2) - nghost
                                                       }),
                                          KOKKOS_CLASS_LAMBDA(const size_t j, 
                                                              const size_t k)
                                          {
                                          view(0, j, k)   = view(N-2, j, k); 
                                          view(N-1, j, k) = view(1, j, k); 
                                          });
                    break;
                case 1:
                    Kokkos::parallel_for("Assign periodic field BC Y", 
                                          mdrange_type({nghost, nghost},
                                                       {view.extent(0) - nghost,
                                                        view.extent(2) - nghost
                                                       }),
                                          KOKKOS_CLASS_LAMBDA(const size_t i, 
                                                              const size_t k)
                                          {
                                          view(i, 0, k)   = view(i, N-2, k); 
                                          view(i, N-1, k) = view(i, 1, k); 
                                          });
                    break;
                case 2:
                    Kokkos::parallel_for("Assign periodic field BC Z", 
                                          mdrange_type({nghost, nghost},
                                                       {view.extent(0) - nghost,
                                                        view.extent(1) - nghost
                                                       }),
                                          KOKKOS_CLASS_LAMBDA(const size_t i, 
                                                              const size_t j)
                                          {
                                          view(i, j, 0)    = view(i, j, N-2); 
                                          view(i, j, N-1)  = view(i, j, 1); 
                                          });
                    break;
                default:
                    throw IpplException("PeriodicFace::apply", "face number wrong");
            }
       }
    }
}
