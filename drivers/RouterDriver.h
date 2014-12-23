/*
 * RouterDriver.h
 *
 *  Created on: Dec 21, 2014
 *      Author: yonch
 */

#ifndef DRIVERS_ROUTERDRIVER_H_
#define DRIVERS_ROUTERDRIVER_H_

class Router;
struct fp_ring;
struct emu_admission_statistics;

class RouterDriver {
public:
	RouterDriver(Router *router, struct fp_ring *q_to_router,
			struct fp_ring *q_from_router,
			struct emu_admission_statistics *stat);

	void step();
private:
	Router *m_router;
	struct fp_ring *m_q_to_router;
	struct fp_ring *m_q_from_router;
	struct emu_admission_statistics	*m_stat;
};

#endif /* DRIVERS_ROUTERDRIVER_H_ */
