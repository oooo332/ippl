#ifndef IPPL_PIC_MANAGER
#define IPPL_PIC_MANAGER

#include <memory>

#include "Manager/BaseManager.h"
 
 namespace ippl {

    template <class ParticleContainer, class FieldContainer, class FieldSolver>
    class PicManager : public BaseManager {
    public:
        PicManager()
            : BaseManager() {}

        virtual ~PicManager() = default;

        virtual void par2grid() = 0;

        virtual void grid2par() = 0;

	//void setStepper(Stepper* stepper) {
	// stepper_m = std::make_unique<Stepper>(stepper);
	//};

	// void setFieldSolver(...);

	// void setLoadBalancer(...);

    protected:
     
        std::shared_ptr<FieldContainer> fcontainer_m;

        std::shared_ptr<ParticleContainer> pcontainer_m;

        //std::unique_ptr<Stepper> stepper_m;

        //std::unique_ptr<LoadBalancer> loadbalancer_m;

        std::shared_ptr<FieldSolver> fsolver_m;
    };
}  // namespace ippl
 
#endif

