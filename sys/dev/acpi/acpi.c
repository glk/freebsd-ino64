/*-
 * Copyright (c) 1999 Takanori Watanabe <takawata@shidahara1.planet.sci.kobe-u.ac.jp>
 * Copyright (c) 1999, 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: acpi.c,v 1.26 2000/08/15 14:43:43 iwasaki Exp $
 *	$FreeBSD$
 */

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/sysctl.h>

#include <sys/malloc.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/eventhandler.h>		/* for EVENTHANDLER_REGISTER */
#include <sys/reboot.h>			/* for RB_POWEROFF */
#include <machine/clock.h>		/* for DELAY */
#include <sys/unistd.h>                 /* for RFSTOPPED*/
#include <sys/kthread.h>                /* for kthread stuff*/
#include <sys/ctype.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <sys/acpi.h>

#include <dev/acpi/acpi.h>		/* for softc */

#include <dev/acpi/aml/aml_amlmem.h>
#include <dev/acpi/aml/aml_common.h>
#include <dev/acpi/aml/aml_env.h>
#include <dev/acpi/aml/aml_evalobj.h>
#include <dev/acpi/aml/aml_name.h>
#include <dev/acpi/aml/aml_parse.h>
#include <dev/acpi/aml/aml_memman.h>

#include <machine/acpi_machdep.h>	/* for ACPI_BUS_SPACE_IO */

/*
 * ACPI pmap subsystem
 */
#define ACPI_SMAP_MAX_SIZE	16

struct ACPIaddr {
	int	entries;
	struct {
		vm_offset_t	pa_base;
		vm_offset_t	va_base;
		vm_size_t	size;
		u_int32_t	type;
	} t [ACPI_SMAP_MAX_SIZE];
};

static void		 acpi_pmap_init(void);
static void		 acpi_pmap_release(void);
static vm_offset_t	 acpi_pmap_ptv(vm_offset_t pa);
static vm_offset_t	 acpi_pmap_vtp(vm_offset_t va);
static void		 acpi_free(struct acpi_softc *sc);

/*
 * These items cannot be in acpi_softc because they are be initialized
 * before probing for the device.
 */

static struct	ACPIaddr acpi_addr;
struct		ACPIrsdp *acpi_rsdp;
static int	acpi_port;
static int	acpi_irq;

/* Character device */

static d_open_t		acpiopen;
static d_close_t	acpiclose;
static d_ioctl_t	acpiioctl;
static d_mmap_t		acpimmap;

#define CDEV_MAJOR 152
static struct cdevsw acpi_cdevsw = {
	acpiopen,
	acpiclose,
	noread,
	nowrite,
	acpiioctl,
	nopoll,
	acpimmap,
	nostrategy,
	"acpi",
	CDEV_MAJOR,
	nodump,
	nopsize,
	0,
	-1
};

/* 
 * ACPI register I/O 
 */
#define	ACPI_REGISTER_INPUT	0
#define	ACPI_REGISTER_OUTPUT	1
static void acpi_register_input(u_int32_t ioaddr,
				u_int32_t *value, u_int32_t size);
static void acpi_register_output(u_int32_t ioaddr,
				 u_int32_t *value, u_int32_t size);
static void acpi_enable_disable(acpi_softc_t *sc, boolean_t enable);
static void acpi_io_pm1_status(acpi_softc_t *sc, boolean_t io,
			       u_int32_t *a, u_int32_t *b);
static void acpi_io_pm1_enable(acpi_softc_t *sc, boolean_t io,
			       u_int32_t *a, u_int32_t *b);
static void acpi_io_pm1_control(acpi_softc_t *sc, boolean_t io,
				u_int32_t *a, u_int32_t *b);
static void acpi_io_pm2_control(acpi_softc_t *sc, boolean_t io, u_int32_t *val);
static void acpi_io_pm_timer(acpi_softc_t *sc, boolean_t io, u_int32_t *val);
static void acpi_io_gpe0_status(acpi_softc_t *sc, boolean_t io, u_int32_t *val);
static void acpi_io_gpe0_enable(acpi_softc_t *sc, boolean_t io, u_int32_t *val);
static void acpi_io_gpe1_status(acpi_softc_t *sc, boolean_t io, u_int32_t *val);
static void acpi_io_gpe1_enable(acpi_softc_t *sc, boolean_t io, u_int32_t *val);

/* 
 * Miscellous utility functions 
 */
static int  acpi_sdt_checksum(struct ACPIsdt * sdt);
static void acpi_handle_dsdt(acpi_softc_t *sc);
static void acpi_handle_facp(acpi_softc_t *sc, struct ACPIsdt *facp);
static int  acpi_handle_rsdt(acpi_softc_t *sc);

/* 
 * System sleeping state
 */
static void acpi_trans_sleeping_state(acpi_softc_t *sc, u_int8_t state);
static void acpi_soft_off(void *data, int howto);
static void acpi_set_sleeping_state(acpi_softc_t *sc, u_int8_t state);
static void acpi_execute_pts(acpi_softc_t *sc, u_int8_t state);
static void acpi_execute_wak(acpi_softc_t *sc, u_int8_t state);

/* 
 * ACPI events
 */
static void acpi_process_event(acpi_softc_t *sc,
			       u_int32_t status_a, u_int32_t status_b,
			       u_int32_t status_0, u_int32_t status_1);
static void acpi_intr(void *data);
static void acpi_enable_events(acpi_softc_t *sc);
static void acpi_clear_ignore_events(void *arg);

/*
 * Bus interface 
 */
static int  acpi_send_pm_event(acpi_softc_t *sc, u_int8_t state);
static void acpi_identify(driver_t *driver, device_t parent);
static int  acpi_probe(device_t dev);
static int  acpi_attach(device_t dev);

/*
 * Event thread
 */
static void acpi_event_thread(void *);
static void acpi_start_threads(void *);

/* for debugging */
#ifdef ACPI_DEBUG
static	int	acpi_debug = 1;
#else	/* !ACPI_DEBUG */
static	int	acpi_debug = 0;
#endif	/* ACPI_DEBUG */

SYSCTL_INT(_debug, OID_AUTO, acpi_debug, CTLFLAG_RW, &acpi_debug, 1, "");

/*
 * ACPI pmap subsystem
 */

void
acpi_init_addr_range()
{
	acpi_addr.entries = 0;
}

void
acpi_register_addr_range(u_int64_t base, u_int64_t size, u_int32_t type)
{
	int		i;
	u_int32_t	ext_size;
	vm_offset_t	pa_base, pa_next_base;

	if (acpi_addr.entries == ACPI_SMAP_MAX_SIZE) {
		return;		/* no room */
	}

	for (i = 0; i < acpi_addr.entries; i++) {
		if (type != acpi_addr.t[i].type) {
			continue;
		}

		pa_base = acpi_addr.t[i].pa_base;
		pa_next_base = pa_base + acpi_addr.t[i].size;

		/* continuous or overlap? */
		if (base > pa_base && base <= pa_next_base) {
			ext_size = size - (pa_next_base - base);
			acpi_addr.t[i].size += ext_size;
			return;
		}
	}

	i = acpi_addr.entries;
	acpi_addr.t[i].pa_base = base;
	acpi_addr.t[i].size = size;
	acpi_addr.t[i].type = type;
	acpi_addr.entries++;
}

static void
acpi_pmap_init()
{
	int		i;
	vm_offset_t	va;

	for (i = 0; i < acpi_addr.entries; i++) {
		va = (vm_offset_t) pmap_mapdev(acpi_addr.t[i].pa_base,
					       acpi_addr.t[i].size);
		acpi_addr.t[i].va_base = va;
		ACPI_DEBUGPRINT("ADDR RANGE %x %x (mapped 0x%x)\n",
				acpi_addr.t[i].pa_base, acpi_addr.t[i].size, va);
	}
}

static void
acpi_pmap_release()
{
#if 0
	int		i;

	for (i = 0; i < acpi_addr.entries; i++) {
		pmap_unmapdev(acpi_addr.t[i].va_base, acpi_addr.t[i].size);
	}
	acpi_addr.entries = 0;
#endif
}

static vm_offset_t
acpi_pmap_ptv(vm_offset_t pa)
{
	int		i;
	vm_offset_t	va;

	va = 0;
	for (i = 0; i < acpi_addr.entries; i++) {
		if (pa >= acpi_addr.t[i].pa_base &&
		    pa < acpi_addr.t[i].pa_base + acpi_addr.t[i].size) {
			va = acpi_addr.t[i].va_base + pa - acpi_addr.t[i].pa_base;
			return (va);
		}
	}

	return (va);
}

static vm_offset_t
acpi_pmap_vtp(vm_offset_t va)
{
	int		i;
	vm_offset_t	pa;

	pa = 0;
	for (i = 0; i < acpi_addr.entries; i++) {
		if (va >= acpi_addr.t[i].va_base &&
		    va < acpi_addr.t[i].va_base + acpi_addr.t[i].size) {
			pa = acpi_addr.t[i].pa_base + va - acpi_addr.t[i].va_base;
			return (pa);
		}
	}

	return (pa);
}

/*
 * ACPI Register I/O
 */
void
acpi_gpe_enable_bit(acpi_softc_t *sc, u_int32_t bit, boolean_t on_off)
{
	int		x;
	u_int32_t	pos, value;
	void		(*GPEx_EN[])(acpi_softc_t *, boolean_t, u_int32_t *) = {
		acpi_io_gpe0_enable, 
		acpi_io_gpe0_enable
	};

	x = -1;
	pos = bit;
	if (bit < sc->facp_body->gpe0_len * 4) {
		x = 0;
	} else {
		/* should we check gpe1_len too? */
		pos = bit - sc->facp_body->gpe1_base;
		x = 1;
	}

	if (x == -1 || (on_off != 0 && on_off != 1)) {
		return;
	}

	GPEx_EN[x](sc, ACPI_REGISTER_INPUT, &value);
	value = (value & (~(1 << pos))) | (on_off << pos);
	GPEx_EN[x](sc, ACPI_REGISTER_OUTPUT, &value);
}

static __inline void
acpi_register_input(u_int32_t ioaddr, u_int32_t *value, u_int32_t size)
{
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	u_int32_t		val;

	bst = ACPI_BUS_SPACE_IO;
	bsh = ioaddr;

	switch (size) {
	case 1:
		val = bus_space_read_1(bst, bsh, 0);
		break;
	case 2:
		val = bus_space_read_2(bst, bsh, 0);
		break;
	case 3:
		val = bus_space_read_4(bst, bsh, 0);
		val &= 0x00ffffff;
		break;
	case 4:
		val = bus_space_read_4(bst, bsh, 0);
		break;
	default:
		ACPI_DEVPRINTF("acpi_register_input(): invalid size\n");
		val = 0;
		break;
	}

	*value = val;
}

static __inline void
acpi_register_output(u_int32_t ioaddr, u_int32_t *value, u_int32_t size)
{
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	u_int32_t		val;

	val = *value;
	bst = ACPI_BUS_SPACE_IO;
	bsh = ioaddr;

	switch (size) {
	case 1:
		bus_space_write_1(bst, bsh, 0, val & 0xff);
		break;
	case 2:
		bus_space_write_2(bst, bsh, 0, val & 0xffff);
		break;
	case 3:
		bus_space_write_2(bst, bsh, 0, val & 0xffff);
		bus_space_write_1(bst, bsh, 2, (val >> 16) & 0xff);
		break;
	case 4:
		bus_space_write_4(bst, bsh, 0, val);
		break;
	default:
		ACPI_DEVPRINTF("acpi_register_output(): invalid size\n");
		break;
	}
}

static void
acpi_enable_disable(acpi_softc_t *sc, boolean_t enable)
{
	u_char			val;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	struct			FACPbody *facp;

	facp = sc->facp_body;
	bst = ACPI_BUS_SPACE_IO;
	bsh = facp->smi_cmd;

	if (enable) {
		val = facp->acpi_enable;
	} else {
		val = facp->acpi_disable;
	}

	bus_space_write_1(bst, bsh, 0, val);
	sc->enabled = enable;

	ACPI_DEBUGPRINT("acpi_enable_disable(%d) = (%x)\n", enable, val);
}

static void
acpi_io_pm1_status(acpi_softc_t *sc, boolean_t io, u_int32_t *status_a,
		   u_int32_t *status_b)
{
	int		size;
	struct FACPbody	*facp;

	facp = sc->facp_body;
	size = facp->pm1_evt_len / 2;

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->pm1a_evt_blk, status_a, size);

		*status_b = 0;
		if (facp->pm1b_evt_blk) {
			acpi_register_input(facp->pm1b_evt_blk, status_b, size);
		}
	} else {
		acpi_register_output(facp->pm1a_evt_blk, status_a, size);

		if (facp->pm1b_evt_blk) {
			acpi_register_output(facp->pm1b_evt_blk, status_b, size);
		}
	}

	ACPI_DEBUGPRINT("acpi_io_pm1_status(%d) = (%x, %x)\n",
			io, *status_a, *status_b);

	return;
}

static void
acpi_io_pm1_enable(acpi_softc_t *sc, boolean_t io, u_int32_t *status_a,
		   u_int32_t *status_b)
{
	int		size;
	struct FACPbody *facp;

	facp = sc->facp_body;
	size = facp->pm1_evt_len / 2;

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->pm1a_evt_blk + size, status_a, size);

		*status_b = 0;
		if (facp->pm1b_evt_blk) {
			acpi_register_input(facp->pm1b_evt_blk + size,
					    status_b, size);
		}
	} else {
		acpi_register_output(facp->pm1a_evt_blk + size, status_a, size);

		if (facp->pm1b_evt_blk) {
			acpi_register_output(facp->pm1b_evt_blk + size,
					     status_b, size);
		}
	}

	ACPI_DEBUGPRINT("acpi_io_pm1_enable(%d) = (%x, %x)\n",
			io, *status_a, *status_b);

	return;
}

static void
acpi_io_pm1_control(acpi_softc_t *sc, boolean_t io, u_int32_t *value_a,
		    u_int32_t *value_b)
{
	int		size;
	struct FACPbody	*facp;

	facp = sc->facp_body;
	size = facp->pm1_cnt_len;

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->pm1a_cnt_blk, value_a, size);

		*value_b = 0;
		if (facp->pm1b_evt_blk) {
			acpi_register_input(facp->pm1b_cnt_blk, value_b, size);
		}
	} else {
		acpi_register_output(facp->pm1a_cnt_blk, value_a, size);

		if (facp->pm1b_evt_blk) {
			acpi_register_output(facp->pm1b_cnt_blk, value_b, size);
		}
	}

	ACPI_DEBUGPRINT("acpi_io_pm1_control(%d) = (%x, %x)\n",
			io, *value_a, *value_b);

	return;
}

static void
acpi_io_pm2_control(acpi_softc_t *sc, boolean_t io, u_int32_t *val)
{
	int		size;
	struct FACPbody	*facp;

	facp = sc->facp_body;
	size = facp->pm2_cnt_len;

	if (!facp->pm2_cnt_blk) {
		return;	/* optional */
	}

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->pm2_cnt_blk, val, size);
	} else {
		acpi_register_output(facp->pm2_cnt_blk, val, size);
	}

	ACPI_DEBUGPRINT("acpi_io_pm2_control(%d) = (%x)\n", io, *val);

	return;
}

static void
acpi_io_pm_timer(acpi_softc_t *sc, boolean_t io, u_int32_t *val)
{
	int		size;
	struct FACPbody	*facp;

	facp = sc->facp_body;
	size = 0x4;	/* 32-bits */

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->pm_tmr_blk, val, size);
	} else {
		return;	/* XXX read-only */
	}

	ACPI_DEBUGPRINT("acpi_io_pm_timer(%d) = (%x)\n", io, *val);

	return;
}

static void
acpi_io_gpe0_status(acpi_softc_t *sc, boolean_t io, u_int32_t *val)
{
	int		size;
	struct	FACPbody *facp;

	facp = sc->facp_body;
	size = facp->gpe0_len / 2;

	if (!facp->gpe0_blk) {
		return;	/* optional */
	}

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->gpe0_blk, val, size);
	} else {
		acpi_register_output(facp->gpe0_blk, val, size);
	}

	ACPI_DEBUGPRINT("acpi_io_gpe0_status(%d) = (%x)\n", io, *val);

	return;
}

static void
acpi_io_gpe0_enable(acpi_softc_t *sc, boolean_t io, u_int32_t *val)
{
	int		size;
	struct	FACPbody *facp;

	facp = sc->facp_body;
	size = facp->gpe0_len / 2;

	if (!facp->gpe0_blk) {
		return;	/* optional */
	}

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->gpe0_blk + size, val, size);
	} else {
		acpi_register_output(facp->gpe0_blk + size, val, size);
	}

	ACPI_DEBUGPRINT("acpi_io_gpe0_enable(%d) = (%x)\n", io, *val);

	return;
}

static void
acpi_io_gpe1_status(acpi_softc_t *sc, boolean_t io, u_int32_t *val)
{
	int		size;
	struct FACPbody	*facp;

	facp = sc->facp_body;
	size = facp->gpe1_len / 2;

	if (!facp->gpe1_blk) {
		return;	/* optional */
	}

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->gpe1_blk, val, size);
	} else {
		acpi_register_output(facp->gpe1_blk, val, size);
	}

	ACPI_DEBUGPRINT("acpi_io_gpe1_status(%d) = (%x)\n", io, *val);

	return;
}

static void
acpi_io_gpe1_enable(acpi_softc_t *sc, boolean_t io, u_int32_t *val)
{
	int		size;
	struct FACPbody	*facp;

	facp = sc->facp_body;
	size = facp->gpe1_len / 2;

	if (!facp->gpe1_blk) {
		return;	/* optional */
	}

	if (io == ACPI_REGISTER_INPUT) {
		acpi_register_input(facp->gpe1_blk + size, val, size);
	} else {
		acpi_register_output(facp->gpe1_blk + size, val, size);
	}

	ACPI_DEBUGPRINT("acpi_io_gpe1_enable(%d) = (%x)\n", io, *val);

	return;
}

/*
 * Miscellous utility functions
 */

static int
acpi_sdt_checksum(struct ACPIsdt *sdt)
{
	u_char	cksm, *ckbf;
	int	i;

	cksm = 0;
	ckbf = (u_char *) sdt;

	for (i = 0; i < sdt->len; i++)
		cksm += ckbf[i];

	return ((cksm == 0) ? 0 : EINVAL);
}

static void
acpi_handle_dsdt(acpi_softc_t *sc)
{
	int					i;
	int					debug;
	struct aml_name				*newname, *sname;
	union aml_object			*spkg;
	struct aml_name_group			*newgrp;
	struct ACPIsdt				*dsdp;
	char					namestr[5];
	struct aml_environ			env;
	struct acpi_system_state_package	ssp;

	/*
	 * Some systems (eg. IBM laptops) expect "Microsoft Windows*" as
	 * \_OS_ string, so we create it anyway.
	 */
	aml_new_name_group(AML_NAME_GROUP_OS_DEFINED);
	env.curname = aml_get_rootname();
	newname = aml_create_name(&env, "\\_OS_");
	newname->property = aml_alloc_object(aml_t_string, NULL);
	newname->property->str.needfree = 0;
	newname->property->str.string = "Microsoft Windows NG";

	/* 
	 * Create namespace. 
	 */
	dsdp = sc->dsdt;
	newgrp = aml_new_name_group((int)dsdp->body);

	bzero(&env, sizeof(env));
	env.dp = (u_int8_t *)dsdp->body;
	env.end = (u_int8_t *)dsdp + dsdp->len;
	env.curname = aml_get_rootname();

	/* 
	 * Suppress debugging during AML parsing.
	 */
	debug = aml_debug;
	aml_debug = 0;

	aml_local_stack_push(aml_local_stack_create());
	aml_parse_objectlist(&env, 0);
	aml_local_stack_delete(aml_local_stack_pop());

	aml_debug = debug;

	if (aml_debug) {
		aml_showtree(aml_get_rootname(), 0);
	}

	/* 
	 * Get sleeping type values from ACPI namespace.
	 */
	sc->system_state = ACPI_S_STATE_S0;
	sc->system_state_initialized = 1;
	for (i = ACPI_S_STATE_S0; i <= ACPI_S_STATE_S5; i++) {
		ssp.mode[i].slp_typ_a = ACPI_UNSUPPORTSLPTYP;
		ssp.mode[i].slp_typ_b = ACPI_UNSUPPORTSLPTYP;
		sprintf(namestr, "_S%d_", i);
		sname = aml_find_from_namespace(NULL, namestr);
		if (sname == NULL) {
			continue;
		}
		spkg = aml_eval_name(&env, sname);
		if (spkg == NULL || spkg->type != aml_t_package) {
			continue;
		}
		if (spkg->package.elements < 2) {
			continue;
		}
		if (spkg->package.objects[0] == NULL ||
		    spkg->package.objects[0]->type != aml_t_num) {
			continue;
		}
		ssp.mode[i].slp_typ_a = spkg->package.objects[0]->num.number;
		ssp.mode[i].slp_typ_b = spkg->package.objects[1]->num.number;
		ACPI_DEBUGPRINT("%s : [%d, %d]\n", namestr,
				ssp.mode[i].slp_typ_a, ssp.mode[i].slp_typ_b);
	}
	sc->system_state_package = ssp;

#if 0
	while (name_group_list->id != AML_NAME_GROUP_ROOT) {
		aml_delete_name_group(name_group_list);
	}
	memman_statistics(aml_memman);
	memman_freeall(aml_memman);
#endif
}

/*
 * Handle the FACP.
 *
 * In the special case where sc is NULL, we are just trying to set acpi_port 
 * and acpi_irq, so don't try to do anything to the softc, and return as soon
 * as we have worked them out.
 */
static void
acpi_handle_facp(acpi_softc_t *sc, struct ACPIsdt *facp)
{
	struct ACPIsdt	*dsdt;
	struct FACPbody	*body;
	struct FACS	*facs;

	body = (struct FACPbody *)facp->body;

	/* in discovery mode, we have everything we need now */
	if (sc == NULL) {
		acpi_port = body->smi_cmd;
		acpi_irq = body->sci_int;
		return;
	}

	ACPI_DEBUGPRINT("	FACP found\n");
	sc->facp = facp;
	sc->facp_body = body;
	sc->dsdt = NULL;
	sc->facs = NULL;
	dsdt = (struct ACPIsdt *) acpi_pmap_ptv(body->dsdt_ptr);
	if (dsdt == NULL) {
		return;
	}

	if (strncmp(dsdt->signature, "DSDT", 4) == 0 &&
	    acpi_sdt_checksum(dsdt) == 0) {
		ACPI_DEBUGPRINT("	DSDT found Size=%d bytes\n", dsdt->len);
		sc->dsdt = dsdt;
		acpi_handle_dsdt(sc);
	}

	facs = (struct FACS *) acpi_pmap_ptv(body->facs_ptr);
	if (facs == NULL) {
		return;
	}

	/*
	 * FACS has no checksum (modified by both OS and BIOS) and in many
	 * cases, it is in nonvolatile storage.
	 */
	if (strncmp(facs->signature, "FACS", 4) == 0) {
		sc->facs = facs;
		ACPI_DEBUGPRINT("	FACS found Size=%d bytes\n", facs->len);
	}
}

/*
 * Handle the RSDT.
 *
 * In the special case where sc is NULL, we are just trying to set acpi_port 
 * and acpi_irq, so don't try to do anything to the softc.
 */
static int
acpi_handle_rsdt(acpi_softc_t *sc)
{
	u_int32_t	*ptrs;
	int		entries;
	int		i;
	struct ACPIsdt	*rsdt, *sdt;
	char		sigstring[5];

	rsdt = (struct ACPIsdt *) acpi_pmap_ptv(acpi_rsdp->addr);
	if (rsdt == 0) {
		ACPI_DEVPRINTF("cannot map physical memory\n");
		return (-1);
	}
	if ((strncmp(rsdt->signature, "RSDT", 4) != 0) ||
	    (acpi_sdt_checksum(rsdt) != 0)) {
		ACPI_DEVPRINTF("RSDT is broken\n");
		return (-1);
	}
	if (sc != NULL)
		sc->rsdt = rsdt;
	entries = (rsdt->len - SIZEOF_SDT_HDR) / sizeof(u_int32_t);
	ACPI_DEBUGPRINT("RSDT have %d entries\n", entries);
	ptrs = (u_int32_t *) & rsdt->body;

	for (i = 0; i < entries; i++) {
		sdt = (struct ACPIsdt *) acpi_pmap_ptv((vm_offset_t) ptrs[i]);
		bzero(sigstring, sizeof(sigstring));
		strncpy(sigstring, sdt->signature, sizeof(sdt->signature));
		ACPI_DEBUGPRINT("RSDT entry%d %s\n", i, sigstring);

		if (strncmp(sdt->signature, "FACP", 4) == 0 &&
		    acpi_sdt_checksum(sdt) == 0) {
			acpi_handle_facp(sc, sdt);
		}
	}

	return (0);
}

/*
 * System sleeping state stuff.
 */

static void
acpi_trans_sleeping_state(acpi_softc_t *sc, u_int8_t state)
{
	u_int8_t	slp_typx;
	u_int32_t	val_a, val_b;
	int		debug, count;
	u_long		ef;

	/* XXX should be MI */
	/* XXX should always be called with interrupts enabled! */
	ef = read_eflags();
	disable_intr();

	if (state > ACPI_S_STATE_S0) {
		/* clear WAK_STS bit by writing a one */
		acpi_io_pm1_status(sc, ACPI_REGISTER_INPUT, &val_a, &val_b);
		if ((val_a | val_b) & ACPI_PM1_WAK_STS) {
			sc->broken_wakeuplogic = 0;
		} else {
			ACPI_DEVPRINTF("wake-up logic seems broken, "
				       "this may cause troubles on wakeup\n");
			sc->broken_wakeuplogic = 1;
		}
		val_a = val_b = 0;
		val_a = ACPI_PM1_WAK_STS;
		val_b = ACPI_PM1_WAK_STS;
		acpi_io_pm1_status(sc, ACPI_REGISTER_OUTPUT, &val_a, &val_b);

		/* ignore power button and sleep button events for 5 sec. */
		sc->ignore_events = ACPI_PM1_PWRBTN_EN | ACPI_PM1_SLPBTN_EN;
		timeout(acpi_clear_ignore_events, (caddr_t)sc, hz * 5);
	}

	acpi_io_pm1_control(sc, ACPI_REGISTER_INPUT, &val_a, &val_b);
	val_a  &= ~(ACPI_CNT_SLP_TYPX);
	val_b  &= ~(ACPI_CNT_SLP_TYPX);
	slp_typx = sc->system_state_package.mode[state].slp_typ_a;
	val_a  |= ACPI_CNT_SET_SLP_TYP(slp_typx) | ACPI_CNT_SLP_EN;
	slp_typx = sc->system_state_package.mode[state].slp_typ_b;
	val_b  |= ACPI_CNT_SET_SLP_TYP(slp_typx) | ACPI_CNT_SLP_EN;
	acpi_io_pm1_control(sc, ACPI_REGISTER_OUTPUT, &val_a, &val_b);

	if (state == ACPI_S_STATE_S0) {
		goto sleep_done;
	}

	/*
	 * wait for WAK_STS bit
	 */
	debug = acpi_debug;	/* Save debug level */
	acpi_debug = 0;		/* Shut up */

	count = 0;
	for (;;) {
		acpi_io_pm1_status(sc, ACPI_REGISTER_INPUT, &val_a, &val_b);
		if ((val_a | val_b) & ACPI_PM1_WAK_STS) {
			break;
		}
		/* XXX
		 * some BIOSes doesn't set WAK_STS at all,
		 * give up waiting for wakeup if timeout...
		 */
		if (sc->broken_wakeuplogic) {
			if (count++ >= 100) {
				break;		/* giving up */
			}
		}
		DELAY(10*1000);			/* 0.01 sec */
	}
	acpi_debug = debug;	/* Restore debug level */

sleep_done:
	/* XXX should be MI */
	write_eflags(ef);
}

static void
acpi_soft_off(void *data, int howto)
{
	acpi_softc_t	*sc;
	u_int32_t	vala = 0;

	if (!(howto & RB_POWEROFF)) {
		return;
	}
	sc = (acpi_softc_t *) data;
	acpi_execute_pts(sc, ACPI_S_STATE_S5);

	/* XXX Disable GPE intrrupt,or power on again in some machine */
	acpi_io_gpe0_enable(sc, ACPI_REGISTER_OUTPUT, &vala);
	acpi_io_gpe1_enable(sc, ACPI_REGISTER_OUTPUT, &vala);

	acpi_trans_sleeping_state(sc, ACPI_S_STATE_S5);
}

static void
acpi_set_sleeping_state(acpi_softc_t *sc, u_int8_t state)
{
	u_int8_t	slp_typ_a, slp_typ_b;

	if (!sc->system_state_initialized) {
		return;
	}

	slp_typ_a = sc->system_state_package.mode[state].slp_typ_a;
	slp_typ_b = sc->system_state_package.mode[state].slp_typ_b;

	if (slp_typ_a == ACPI_UNSUPPORTSLPTYP ||
	    slp_typ_b == ACPI_UNSUPPORTSLPTYP) {
		return;	/* unsupported sleeping type */
	}

	if (state < ACPI_S_STATE_S5) {
		/* inform all devices that we are going to sleep. */
		if (acpi_send_pm_event(sc, state) != 0) {
			/* if failure, 'wakeup' the system again */
			acpi_send_pm_event(sc, ACPI_S_STATE_S0);
			return;
		}

		/* Prepare to sleep */
		acpi_execute_pts(sc, state);

		/* PowerResource manipulation */
		acpi_powerres_set_sleeping_state(sc, state);
		if (acpi_debug) {
			acpi_powerres_debug(sc);
		}

		sc->system_state = state;
	}

	/*
	 * XXX currently support S1 and S5 only.
	 */
	switch (state) {
	case ACPI_S_STATE_S0:
	case ACPI_S_STATE_S1:
		acpi_trans_sleeping_state(sc, state);
		break;
	case ACPI_S_STATE_S5:
		/* Power the system off using ACPI */
		shutdown_nice(RB_POWEROFF);
		break;
	default:
		break;
	}

	if (state < ACPI_S_STATE_S5) {
		acpi_powerres_set_sleeping_state(sc, 0);
		if (acpi_debug) {
			acpi_powerres_debug(sc);
		}
		acpi_execute_wak(sc, state);
		acpi_send_pm_event(sc, ACPI_S_STATE_S0);

		sc->system_state = ACPI_S_STATE_S0;
	}
}

static void
acpi_execute_pts(acpi_softc_t *sc, u_int8_t state)
{
	union aml_object	argv[1], *retval;

	argv[0].type = aml_t_num;
	argv[0].num.number = state;
	aml_local_stack_push(aml_local_stack_create());
	retval = aml_invoke_method_by_name("_PTS", 1, argv);
	aml_local_stack_delete(aml_local_stack_pop());
}

static void
acpi_execute_wak(acpi_softc_t *sc, u_int8_t state)
{
	union aml_object	argv[1], *retval;

	argv[0].type = aml_t_num;
	argv[0].num.number = state;
	aml_local_stack_push(aml_local_stack_create());
	retval = aml_invoke_method_by_name("_WAK", 1, argv);
	aml_local_stack_delete(aml_local_stack_pop());

	/* 
	 * XXX These shouldn't be here, but tentatively implemented.
	 * Sample application of aml_apply_foreach_found_objects().
	 * We try to find and evaluate all of objects which have specified
	 * string. As the result, Battery Information, Battery Status and
	 * Power source will be reported.
	 */
	aml_apply_foreach_found_objects(NULL, "_BIF", aml_eval_name_simple);
	aml_apply_foreach_found_objects(NULL, "_BST", aml_eval_name_simple);
	aml_apply_foreach_found_objects(NULL, "_PSR", aml_eval_name_simple);
}

/*
 * ACPI events
 */

static void
acpi_process_event(acpi_softc_t *sc, u_int32_t status_a, u_int32_t status_b,
		   u_int32_t status_0, u_int32_t status_1)
{
	int i;
	
	if (status_a & ACPI_PM1_PWRBTN_EN || status_b & ACPI_PM1_PWRBTN_EN) {
		if (sc->ignore_events & ACPI_PM1_PWRBTN_EN) {
			ACPI_DEBUGPRINT("PWRBTN event ingnored\n");
		} else {
#if 1
			acpi_set_sleeping_state(sc, ACPI_S_STATE_S5);
#else
			/*
			 * If there is ACPI userland daemon,
			 * this event should be passed to it
			 * so that the user can determine power policy.
			 */
			acpi_queue_event(sc, ACPI_EVENT_TYPE_FIXEDREG, 0);
#endif
		}
	}

	if (status_a & ACPI_PM1_SLPBTN_EN || status_b & ACPI_PM1_SLPBTN_EN) {
		if (sc->ignore_events & ACPI_PM1_SLPBTN_EN) {
			ACPI_DEBUGPRINT("SLPBTN event ingnored\n");
		} else {
#if 1
			acpi_set_sleeping_state(sc, ACPI_S_STATE_S1);
#else
			acpi_queue_event(sc, ACPI_EVENT_TYPE_FIXEDREG, 1);
#endif
		}
	}
	for(i = 0; i < sc->facp_body->gpe0_len * 4; i++)
		if((status_0 & (1 << i)) && (sc->gpe0_mask & (1 << i)))
			acpi_queue_event(sc, ACPI_EVENT_TYPE_GPEREG, i);
	for(i = 0; i < sc->facp_body->gpe1_len * 4 ; i++)
		if((status_1 & (1 << i)) && (sc->gpe1_mask & (1 << i)))
			acpi_queue_event(sc, ACPI_EVENT_TYPE_GPEREG,
					 i + sc->facp_body->gpe1_base);
}

static void
acpi_intr(void *data)
{
	u_int32_t	enable_a, enable_b, enable_0, enable_1;
	u_int32_t	status_a, status_b, status_0, status_1;
	u_int32_t	val_a, val_b;
	int		debug;
	acpi_softc_t	*sc;

	sc = (acpi_softc_t *)data;
	debug = acpi_debug;	/* Save debug level */
	acpi_debug = 0;		/* Shut up */

	/* 
	 * Power Management 1 Status Registers 
	 */
	status_a = status_b = enable_a = enable_b = 0;
	acpi_io_pm1_status(sc, ACPI_REGISTER_INPUT, &status_a, &status_b);

	/* 
	 * Get current interrupt mask
	 */
	acpi_io_pm1_enable(sc, ACPI_REGISTER_INPUT, &enable_a, &enable_b);

	/* 
	 * Disable events and re-enable again
	 */
	if ((status_a & enable_a) != 0 || (status_b & enable_b) != 0) {
		acpi_debug = debug;	/* OK, you can speak */

		ACPI_DEBUGPRINT("pm1_status intr CALLED\n");

		/* Disable all interrupt generation */
		val_a = enable_a & (~ACPI_PM1_ALL_ENABLE_BITS);
		val_b = enable_b & (~ACPI_PM1_ALL_ENABLE_BITS);
		acpi_io_pm1_enable(sc, ACPI_REGISTER_OUTPUT, &val_a, &val_b);

		/* Clear interrupt status */
		val_a = enable_a & ACPI_PM1_ALL_ENABLE_BITS;
		val_b = enable_b & ACPI_PM1_ALL_ENABLE_BITS;
		acpi_io_pm1_status(sc, ACPI_REGISTER_OUTPUT, &val_a, &val_b);

		/* Re-enable interrupt */
		acpi_io_pm1_enable(sc, ACPI_REGISTER_OUTPUT,
				   &enable_a, &enable_b);

		acpi_debug = 0;		/* Shut up again */
	}

	/* 
	 * General-Purpose Events 0 Status Registers
	 */
	status_0 = enable_0 = 0;
	acpi_io_gpe0_status(sc, ACPI_REGISTER_INPUT, &status_0);

	/* 
	 * Get current interrupt mask 
	 */
	acpi_io_gpe0_enable(sc, ACPI_REGISTER_INPUT, &enable_0);

	/* 
	 * Disable events and re-enable again 
	 */
	if ((status_0 & enable_0) != 0) {
		acpi_debug = debug;	/* OK, you can speak */

		ACPI_DEBUGPRINT("gpe0_status intr CALLED\n");

		/* Disable all interrupt generation */
		val_a = enable_0 & ~status_0;
#if 0
		/* or should we disable all? */
		val_a = 0x0;
#endif
		acpi_io_gpe0_enable(sc, ACPI_REGISTER_OUTPUT, &val_a);
#if 0
		/* Clear interrupt status */
		val_a = enable_0;	/* XXX */
		acpi_io_gpe0_status(sc, ACPI_REGISTER_OUTPUT, &val_a);

		/* Re-enable interrupt */
		acpi_io_gpe0_enable(sc, ACPI_REGISTER_OUTPUT, &enable_0);

		acpi_debug = 0;		/* Shut up again */
#endif
	}

	/*
	 * General-Purpose Events 1 Status Registers
	 */
	status_1 = enable_1 = 0;
	acpi_io_gpe1_status(sc, ACPI_REGISTER_INPUT, &status_1);

	/*
	  Get current interrupt mask
	*/
	acpi_io_gpe1_enable(sc, ACPI_REGISTER_INPUT, &enable_1);

	/*
	 * Disable events and re-enable again
	 */
	if ((status_1 & enable_1) != 0) {
		acpi_debug = debug;	/* OK, you can speak */

		ACPI_DEBUGPRINT("gpe1_status intr CALLED\n");

		/* Disable all interrupt generation */
		val_a = enable_1 & ~status_1;
#if 0
		/* or should we disable all? */
		val_a = 0x0;
#endif
		acpi_io_gpe1_enable(sc, ACPI_REGISTER_OUTPUT, &val_a);

		/* Clear interrupt status */
		val_a = enable_1;	/* XXX */
		acpi_io_gpe1_status(sc, ACPI_REGISTER_OUTPUT, &val_a);

		/* Re-enable interrupt */
		acpi_io_gpe1_enable(sc, ACPI_REGISTER_OUTPUT, &enable_1);

		acpi_debug = 0;		/* Shut up again */
	}

	acpi_debug = debug;	/* Restore debug level */

	/* do something to handle the events... */
	acpi_process_event(sc, status_a, status_b, status_0, status_1);
}

static int acpi_set_gpe_bits(struct aml_name *name, va_list ap)
{
	struct acpi_softc	*sc = va_arg(ap, struct acpi_softc *);
	int			*gpemask0 = va_arg(ap, int *);
	int			*gpemask1 = va_arg(ap, int *);
	int			gpenum;

#define XDIGITTONUM(c) ((isdigit(c)) ? ((c) - '0') : ('A' <= (c)&& (c) <= 'F') ? ((c) - 'A' + 10) : 0)

	if (isxdigit(name->name[2]) && isxdigit(name->name[3])) {
 		gpenum = XDIGITTONUM(name->name[2]) * 16 + 
			XDIGITTONUM(name->name[3]);
		ACPI_DEBUGPRINT("GPENUM %d %d \n", gpenum, sc->facp_body->gpe0_len * 4);
		if (gpenum < (sc->facp_body->gpe0_len * 4)) {
			*gpemask0 |= (1 << gpenum);
		} else {
			*gpemask1 |= (1 << (gpenum - sc->facp_body->gpe1_base));
		}
	}
	ACPI_DEBUGPRINT("GPEMASK %x %x\n", *gpemask0, *gpemask1);
	return 0;
}

static void
acpi_enable_events(acpi_softc_t *sc)
{
	u_int32_t	status_a, status_b;
	u_int32_t	flags;

	/*
	 * Setup PM1 Enable Registers Fixed Feature Enable Bits (4.7.3.1.2)
	 * based on flags field of Fixed ACPI Description Table (5.2.5).
	 */
	acpi_io_pm1_enable(sc, ACPI_REGISTER_INPUT, &status_a, &status_b);
	flags = sc->facp_body->flags;
	if ((flags & ACPI_FACP_FLAG_PWR_BUTTON) == 0) {
		status_a |= ACPI_PM1_PWRBTN_EN;
		status_b |= ACPI_PM1_PWRBTN_EN;
	}
	if ((flags & ACPI_FACP_FLAG_SLP_BUTTON) == 0) {
		status_a |= ACPI_PM1_SLPBTN_EN;
		status_b |= ACPI_PM1_SLPBTN_EN;
	}
	acpi_io_pm1_enable(sc, ACPI_REGISTER_OUTPUT, &status_a, &status_b);

#if 1
	/*
	 * XXX
	 * This should be done based on level event handlers in
	 * \_GPE scope (4.7.2.2.1.2).
	 */

	status_a = status_b = 0;
	aml_apply_foreach_found_objects(NULL, "\\_GPE._L", acpi_set_gpe_bits,
					sc, &status_a, &status_b);
	sc->gpe0_mask = status_a;
	sc->gpe1_mask = status_b;

	acpi_io_gpe0_enable(sc, ACPI_REGISTER_OUTPUT, &status_a);
	acpi_io_gpe1_enable(sc, ACPI_REGISTER_OUTPUT, &status_b);
#endif

	/* print all event status for debugging */
	acpi_io_pm1_status(sc, ACPI_REGISTER_INPUT, &status_a, &status_b);
	acpi_io_pm1_enable(sc, ACPI_REGISTER_INPUT,  &status_a, &status_b);
	acpi_io_gpe0_status(sc, ACPI_REGISTER_INPUT, &status_a);
	acpi_io_gpe0_enable(sc, ACPI_REGISTER_INPUT, &status_a);
	acpi_io_gpe1_status(sc, ACPI_REGISTER_INPUT, &status_a);
	acpi_io_gpe1_enable(sc, ACPI_REGISTER_INPUT, &status_a);
	acpi_io_pm1_control(sc, ACPI_REGISTER_INPUT,  &status_a, &status_b);
	acpi_io_pm2_control(sc, ACPI_REGISTER_INPUT,  &status_a);
	acpi_io_pm_timer(sc, ACPI_REGISTER_INPUT,  &status_a);
}

static void
acpi_clear_ignore_events(void *arg)
{
	((acpi_softc_t *)arg)->ignore_events = 0;
	ACPI_DEBUGPRINT("ignore events cleared\n");
}

/*
 * Character device
 */

static int
acpiopen(dev_t dev, int flag, int fmt, struct proc * p)
{
	return (0);
}

static int
acpiclose(dev_t dev, int flag, int fmt, struct proc * p)
{
	return (0);
}

static int
acpiioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc * p)
{
	int		error, state;
	acpi_softc_t	*sc = (struct acpi_softc *)dev->si_drv1;

	error = 0;

	if (sc == NULL) {
		return (EINVAL);
	}

	switch (cmd) {
	case ACPIIO_ENABLE:
		acpi_enable_disable(sc, 1);
		acpi_enable_events(sc);
		break;

	case ACPIIO_DISABLE:
		acpi_enable_disable(sc, 0);
		break;

	case ACPIIO_SETSLPSTATE:
		state = *(int *)addr;
		if (state >= ACPI_S_STATE_S0 && state <= ACPI_S_STATE_S5) {
			acpi_set_sleeping_state(sc, state);
		} else {
			error = EINVAL;
		}
		ACPI_DEBUGPRINT("ACPIIO_SETSLPSTATE = %d\n", state);
		break;

	default:
		error = EINVAL;
		break;
	}

	return (error);
}

static int
acpimmap(dev_t dev, vm_offset_t offset, int nprot)
{
	acpi_softc_t	*sc = (struct acpi_softc *)dev->si_drv1;

	if (sc == NULL) {
		return (EINVAL);
	}
	/* XXX should be MI */
	return (i386_btop(acpi_pmap_vtp((vm_offset_t)(sc->dsdt + offset))));
}

/*
 * Bus interface
 */

static devclass_t acpi_devclass;

static int
acpi_send_pm_event(acpi_softc_t *sc, u_int8_t state)
{
	int	error;

	error = 0;
	switch (state) {
	case ACPI_S_STATE_S0:
		if (sc->system_state != ACPI_S_STATE_S0) {
			DEVICE_RESUME(root_bus);
		}
		break;
	case ACPI_S_STATE_S1:
	case ACPI_S_STATE_S2:
	case ACPI_S_STATE_S3:
	case ACPI_S_STATE_S4:
		error = DEVICE_SUSPEND(root_bus);
		break;
	default:
		break;
	}

	return (error);
}

static void
acpi_identify(driver_t *driver, device_t parent)
{
	device_t        child;

	/*
	 * If the MD code hasn't detected an RSDT, or there has already 
	 * been an 'acpi' device instantiated, give up now.
	 */
	if ((acpi_rsdp == NULL) || (device_find_child(parent, "acpi", 0)))
		return;

	/*
	 * Handle enough of the RSDT to work out what our I/O resources
	 * are.
	 */
	acpi_pmap_init();
	if (acpi_handle_rsdt(NULL))
		return;

	/*
	 * Create the device and establish its resources.
	 */
	if ((child = BUS_ADD_CHILD(parent, 101, "acpi", 0)) == NULL) {
		device_printf(parent, "could not attach ACPI\n");
		return;
	}
	if (acpi_port != 0)
		bus_set_resource(child, SYS_RES_IOPORT, 0, acpi_port, 1);
	if (acpi_irq != 0)
		bus_set_resource(child, SYS_RES_IRQ, 0, acpi_irq, 1);
}

static int
acpi_probe(device_t dev)
{
	int	debug;
	char	oemstring[7];

	/* get debug variables specified in loader. */
	if (getenv_int("debug.acpi_debug", &debug)) {
		acpi_debug = debug;
	}
	if (getenv_int("debug.aml_debug", &debug)) {
		aml_debug = debug;
	}

	bzero(oemstring, sizeof(oemstring));
	strncpy(oemstring, acpi_rsdp->oem, sizeof(acpi_rsdp->oem));

	ACPI_DEBUGPRINT("Found ACPI BIOS data at %p (<%s>, RSDT@%x)\n",
			acpi_rsdp, oemstring, acpi_rsdp->addr);

	device_set_desc_copy(dev, oemstring);
	return (0);
}

static int
acpi_attach(device_t dev)
{
	acpi_softc_t	*sc;

	/*
	 * Set up the softc and parse the ACPI data completely.
	 */
	sc = device_get_softc(dev);
	sc->dev = dev;
	if (acpi_handle_rsdt(sc) != 0) {
		acpi_free(sc);
		return (ENXIO);
	}
	/*
	 * Allocate our resources
	 */
	sc->port_rid = 0;
	if ((sc->port = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->port_rid, 
					   0, ~0, 1, RF_ACTIVE)) == NULL) {
		ACPI_DEVPRINTF("could not allocate port\n");
		acpi_free(sc);
		return(ENOMEM);
	}
	sc->irq_rid = 0;
	if ((sc->irq = bus_alloc_resource(dev, SYS_RES_IRQ, &sc->irq_rid, 
					  0, ~0, 1, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		ACPI_DEVPRINTF("could not allocate interrupt\n");
		acpi_free(sc);
		return(ENOMEM);
	}
	if (bus_setup_intr(dev, sc->irq, INTR_TYPE_MISC, acpi_intr, sc, &sc->irq_handle)) {
		ACPI_DEVPRINTF("could not set up irq\n");
		acpi_free(sc);
		return(ENXIO);
	}

	/* initialise the event queue */
	STAILQ_INIT(&sc->event);

#ifndef ACPI_NO_ENABLE_ON_BOOT
	acpi_enable_disable(sc, 1);
	acpi_enable_events(sc);
	acpi_intr((void *)sc);
#endif


	acpi_powerres_init(sc);
	if (acpi_debug) {
		acpi_powerres_debug(sc);
	}

	EVENTHANDLER_REGISTER(shutdown_final, acpi_soft_off, sc, SHUTDOWN_PRI_LAST);

	acpi_pmap_release();

	sc->dev_t = make_dev(&acpi_cdevsw, 0, 0, 5, 0660, "acpi");
	sc->dev_t->si_drv1 = sc;
	
	/*
	 * Probe/attach children now that the AML has been parsed.
	 */
	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
acpi_detach(device_t dev)
{
	acpi_softc_t	*sc = device_get_softc(dev);
	
	/* acpi_disable_events(sc); */
	acpi_enable_disable(sc, 0);
	acpi_free(sc);

	return(0);
}

static void
acpi_free(struct acpi_softc *sc)
{
	if (sc->port != NULL)
		bus_release_resource(sc->dev, SYS_RES_IOPORT, sc->port_rid, sc->port);
	if (sc->irq_handle != NULL)
		bus_teardown_intr(sc->dev, sc->irq, sc->irq_handle);
	if (sc->irq != NULL)
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid, sc->irq);
	acpi_pmap_release();
}

static int
acpi_resume(device_t dev)
{
	acpi_softc_t	*sc;

	sc = device_get_softc(dev);
	if (sc->enabled) {
		/* re-enable on wakeup */
		acpi_enable_disable(sc, 1);
		acpi_enable_events(sc);
	}
	return (0);
}

static device_method_t acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	acpi_identify),
	DEVMETHOD(device_probe,		acpi_probe),
	DEVMETHOD(device_attach,	acpi_attach),
	DEVMETHOD(device_resume,	acpi_resume),
	DEVMETHOD(device_detach,	acpi_detach),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	{0, 0}
};

static driver_t acpi_driver = {
	"acpi",
	acpi_methods,
	sizeof(acpi_softc_t),
};

DRIVER_MODULE(acpi, nexus, acpi_driver, acpi_devclass, 0, 0);
/*
 *KTHREAD 
 *This part is mostly stolen from NEWCARD pcic code.
 */

void acpi_queue_event(acpi_softc_t *sc, int type, int arg)
{
	struct acpi_event	*ae;
	int			s;

	ae = malloc(sizeof(*ae), M_TEMP, M_NOWAIT);
	if(ae == NULL)
		panic("acpi_queue_event: can't allocate event");
	
	ae->ae_type = type;
	ae->ae_arg = arg;
	s = splhigh();
	STAILQ_INSERT_TAIL(&sc->event, ae, ae_q);
	splx(s);
	wakeup(&sc->event);
}

void acpi_event_thread(void *arg)
{
	acpi_softc_t		*sc = arg;
	int			s , gpe1_base = sc->facp_body->gpe1_base;
	u_int32_t		status,bit;
	struct acpi_event	*ae;
	const char		numconv[] = {'0','1','2','3','4','5','6','7',
					     '8','9','A','B','C','D','E','F',-1};
	char			gpemethod[] = "\\_GPE._LXX";
	union aml_object	argv;	/* Dummy*/

	while(1) {
		s = splhigh();
		if ((ae = STAILQ_FIRST(&sc->event)) == NULL) {
			splx(s);
			tsleep(&sc->event, PWAIT, "acpiev", 0);
			continue;
		} else {
			splx(s);
		}
		s = splhigh();
		STAILQ_REMOVE_HEAD_UNTIL(&sc->event, ae, ae_q);
		splx(s);
		switch(ae->ae_type) {
		case ACPI_EVENT_TYPE_GPEREG:
			sprintf(gpemethod, "\\_GPE._L%c%c",
				numconv[(ae->ae_arg / 0x10) & 0xf],
				numconv[ae->ae_arg & 0xf]);
			aml_invoke_method_by_name(gpemethod, 0, &argv);
			sprintf(gpemethod, "\\_GPE._E%c%c",
				numconv[(ae->ae_arg / 0x10) & 0xf],
				numconv[ae->ae_arg & 0xf]);
			aml_invoke_method_by_name(gpemethod, 0, &argv);
			s=splhigh();
			if((ae->ae_arg < gpe1_base) || (gpe1_base == 0)){
				bit = 1 << ae->ae_arg;
				ACPI_DEBUGPRINT("GPE0%x\n", bit);
				acpi_io_gpe0_status(sc, ACPI_REGISTER_OUTPUT,
						    &bit);
				acpi_io_gpe0_enable(sc, ACPI_REGISTER_INPUT,
						    &status);
				ACPI_DEBUGPRINT("GPE0%x\n", status);
				status |= bit;
				acpi_io_gpe0_enable(sc, ACPI_REGISTER_OUTPUT,
						    &status);
			} else {
				bit = 1 << (ae->ae_arg - sc->facp_body->gpe1_base);
				acpi_io_gpe1_status(sc, ACPI_REGISTER_OUTPUT,
						    &bit);
				acpi_io_gpe1_enable(sc, ACPI_REGISTER_INPUT,
						    &status);
				status |= bit;
				acpi_io_gpe1_enable(sc, ACPI_REGISTER_OUTPUT,
						    &status);
			}
			splx(s);
			break;
		}
		free(ae, M_TEMP);
	}
	ACPI_DEVPRINTF("????\n");
}

void acpi_start_threads(void *arg)
{
	acpi_softc_t	*sc = devclass_get_softc(acpi_devclass, 0);
	device_t	dev = devclass_get_device(acpi_devclass, 0);

	ACPI_DEBUGPRINT("start thread\n");
	if (kthread_create(acpi_event_thread, sc, &sc->acpi_thread, 0, "acpi")) {
		ACPI_DEVPRINTF("CANNOT CREATE THREAD\n");
	}
}

SYSINIT(acpi, SI_SUB_KTHREAD_IDLE, SI_ORDER_ANY, acpi_start_threads, 0);



