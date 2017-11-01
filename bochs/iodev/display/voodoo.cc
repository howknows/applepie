/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2012-2017  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
/////////////////////////////////////////////////////////////////////////

/*
 *  Portion of this software comes with the following license
 */

/***************************************************************************

    Copyright Aaron Giles
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in
          the documentation and/or other materials provided with the
          distribution.
        * Neither the name 'MAME' nor the names of its contributors may be
          used to endorse or promote products derived from this software
          without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY AARON GILES ''AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL AARON GILES BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

// 3dfx Voodoo Graphics (SST-1/2) emulation (based on a patch for DOSBox)

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include "iodev.h"
#if BX_SUPPORT_PCI && BX_SUPPORT_VOODOO

#include "pci.h"
#include "vgacore.h"
#include "voodoo.h"
#include "virt_timer.h"
#include "bxthread.h"

#define LOG_THIS theVoodooDevice->

const Bit8u banshee_iomask[256] = {4,0,0,0,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1,
                                   7,1,3,1,7,1,3,1,7,1,3,1,7,1,3,1};

bx_voodoo_c* theVoodooDevice = NULL;
bx_voodoo_vga_c* theVoodooVga = NULL;

#include "voodoo_types.h"
#include "voodoo_data.h"
#include "voodoo_main.h"
voodoo_state *v;
#include "voodoo_func.h"

// builtin configuration handling functions

void voodoo_init_options(void)
{
  static const char *voodoo_model_list[] = {
    "voodoo1",
    "voodoo2",
    "banshee",
    NULL
  };

  bx_param_c *display = SIM->get_param("display");
  bx_list_c *menu = new bx_list_c(display, "voodoo", "Voodoo Graphics");
  menu->set_options(menu->SHOW_PARENT);
  bx_param_bool_c *enabled = new bx_param_bool_c(menu,
    "enabled",
    "Enable Voodoo Graphics emulation",
    "Enables the 3dfx Voodoo Graphics emulation",
    1);
  new bx_param_enum_c(menu,
    "model",
    "Voodoo model",
    "Selects the Voodoo model to emulate.",
    voodoo_model_list,
    VOODOO_1, VOODOO_1);
  enabled->set_dependent_list(menu->clone());
}

Bit32s voodoo_options_parser(const char *context, int num_params, char *params[])
{
  if (!strcmp(params[0], "voodoo")) {
    bx_list_c *base = (bx_list_c*) SIM->get_param(BXPN_VOODOO);
    for (int i = 1; i < num_params; i++) {
      if (SIM->parse_param_from_list(context, params[i], base) < 0) {
        BX_ERROR(("%s: unknown parameter for voodoo ignored.", context));
      }
    }
  } else {
    BX_PANIC(("%s: unknown directive '%s'", context, params[0]));
  }
  return 0;
}

Bit32s voodoo_options_save(FILE *fp)
{
  return SIM->write_param_list(fp, (bx_list_c*) SIM->get_param(BXPN_VOODOO), NULL, 0);
}

// device plugin entry points

int CDECL libvoodoo_LTX_plugin_init(plugin_t *plugin, plugintype_t type)
{
  theVoodooDevice = new bx_voodoo_c();
  if (type == PLUGTYPE_VGA) {
    theVoodooVga = new bx_voodoo_vga_c();
    bx_devices.pluginVgaDevice = theVoodooVga;
    BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theVoodooVga, BX_PLUGIN_VOODOO);
  } else {
    BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theVoodooDevice, BX_PLUGIN_VOODOO);
  }
  // add new configuration parameter for the config interface
  voodoo_init_options();
  // register add-on option for bochsrc and command line
  SIM->register_addon_option("voodoo", voodoo_options_parser, voodoo_options_save);
  return 0; // Success
}

void CDECL libvoodoo_LTX_plugin_fini(void)
{
  SIM->unregister_addon_option("voodoo");
  bx_list_c *menu = (bx_list_c*)SIM->get_param("display");
  menu->remove("voodoo");
  if (theVoodooVga != NULL) {
    delete theVoodooVga;
  }
  delete theVoodooDevice;
}

// FIFO thread

BX_THREAD_FUNC(fifo_thread, indata)
{
  Bit32u type, offset = 0, data = 0, regnum;
  fifo_state *fifo;

  UNUSED(indata);
  while (1) {
    if (bx_wait_for_event(&fifo_wakeup)) {
      BX_LOCK(fifo_mutex);
      while (1) {
        if (!fifo_empty(&v->fbi.fifo)) {
          fifo = &v->fbi.fifo;
        } else if (!fifo_empty(&v->pci.fifo)) {
          fifo = &v->pci.fifo;
        } else {
          break;
        }
        type = fifo_remove(fifo, &offset, &data);
        BX_UNLOCK(fifo_mutex);
        switch (type) {
          case FIFO_WR_REG:
            if ((offset & 0x800c0) == 0x80000 && v->alt_regmap)
              regnum = register_alias_map[offset & 0x3f];
            else
              regnum = offset & 0xff;
            register_w(offset, data, 0);
            if ((regnum == triangleCMD) || (regnum == ftriangleCMD) || (regnum == nopCMD) ||
                (regnum == fastfillCMD) || (regnum == swapbufferCMD)) {
              BX_LOCK(fifo_mutex);
              v->pci.op_pending--;
              BX_UNLOCK(fifo_mutex);
            }
            break;
          case FIFO_WR_TEX:
            texture_w(offset, data);
            break;
          case FIFO_WR_FBI_32:
            lfb_w(offset, data, 0xffffffff);
            break;
          case FIFO_WR_FBI_16L:
            lfb_w(offset, data, 0x0000ffff);
            break;
          case FIFO_WR_FBI_16H:
            lfb_w(offset, data, 0xffff0000);
            break;
        }
        BX_LOCK(fifo_mutex);
      }
      v->pci.op_pending = 0;
      BX_UNLOCK(fifo_mutex);
      if (v->fbi.cmdfifo[0].enabled) {
        BX_LOCK(cmdfifo_mutex);
        while (v->fbi.cmdfifo[0].cmd_ready) {
          cmdfifo_process();
        }
        BX_UNLOCK(cmdfifo_mutex);
      }
    }
  }
  BX_THREAD_EXIT;
}

// the device object

bx_voodoo_c::bx_voodoo_c()
{
  put("VOODOO");
  s.mode_change_timer_id = BX_NULL_TIMER_HANDLE;
  s.vertical_timer_id = BX_NULL_TIMER_HANDLE;
  v = NULL;
}

bx_voodoo_c::~bx_voodoo_c()
{
  BX_THREAD_KILL(fifo_thread_var);
  BX_FINI_MUTEX(fifo_mutex);
  if (BX_VOODOO_THIS s.model == VOODOO_2) {
    BX_FINI_MUTEX(cmdfifo_mutex);
  }
  bx_destroy_event(&fifo_wakeup);
  bx_destroy_event(&fifo_not_full);

  if (v != NULL) {
    free(v->fbi.ram);
    if (BX_VOODOO_THIS s.model < VOODOO_BANSHEE) {
      free(v->tmu[0].ram);
      free(v->tmu[1].ram);
    }
    delete v;
  }

  BX_DEBUG(("Exit"));
}

void bx_voodoo_c::init(void)
{
  // Read in values from config interface
  bx_list_c *base = (bx_list_c*) SIM->get_param(BXPN_VOODOO);
  // Check if the device is disabled or not configured
  if (!SIM->get_param_bool("enabled", base)->get()) {
    BX_INFO(("Voodoo disabled"));
    // mark unused plugin for removal
    ((bx_param_bool_c*)((bx_list_c*)SIM->get_param(BXPN_PLUGIN_CTRL))->get_by_name("voodoo"))->set(0);
    return;
  }
  BX_VOODOO_THIS s.model = (Bit8u)SIM->get_param_enum("model", base)->get();
  BX_VOODOO_THIS s.devfunc = 0x00;
  if (BX_VOODOO_THIS s.model != VOODOO_BANSHEE) {
    DEV_register_pci_handlers(this, &BX_VOODOO_THIS s.devfunc, BX_PLUGIN_VOODOO,
                              "Experimental 3dfx Voodoo Graphics (SST-1/2)");
    if (BX_VOODOO_THIS s.model == VOODOO_1) {
      init_pci_conf(0x121a, 0x0001, 0x02, 0x000000, 0x00);
    } else if (BX_VOODOO_THIS s.model == VOODOO_2) {
      init_pci_conf(0x121a, 0x0002, 0x02, 0x038000, 0x00);
      BX_VOODOO_THIS pci_conf[0x10] = 0x08;
    }
    BX_VOODOO_THIS pci_conf[0x3d] = BX_PCI_INTA;
    BX_VOODOO_THIS pci_base_address[0] = 0;
  } else {
    if (theVoodooVga == NULL) {
      BX_PANIC(("Voodoo Banshee with VGA disabled not supported yet"));
    }
    DEV_register_pci_handlers(this, &BX_VOODOO_THIS s.devfunc, BX_PLUGIN_VOODOO,
                              "Experimental 3dfx Voodoo Banshee");
    init_pci_conf(0x121a, 0x0003, 0x01, 0x030000, 0x00);
    BX_VOODOO_THIS pci_conf[0x14] = 0x08;
    BX_VOODOO_THIS pci_conf[0x18] = 0x01;
    BX_VOODOO_THIS pci_conf[0x3d] = BX_PCI_INTA;
    BX_VOODOO_THIS pci_base_address[0] = 0;
    BX_VOODOO_THIS pci_base_address[1] = 0;
    BX_VOODOO_THIS pci_base_address[2] = 0;
    BX_VOODOO_THIS pci_rom_address = 0;
    BX_VOODOO_THIS load_pci_rom(SIM->get_param_string(BXPN_VGA_ROM_PATH)->getptr());
  }
  if (BX_VOODOO_THIS s.mode_change_timer_id == BX_NULL_TIMER_HANDLE) {
    BX_VOODOO_THIS s.mode_change_timer_id = bx_virt_timer.register_timer(this, mode_change_timer_handler,
       1000, 0, 0, 0, "voodoo_mode_change");
  }
  if (BX_VOODOO_THIS s.vertical_timer_id == BX_NULL_TIMER_HANDLE) {
    BX_VOODOO_THIS s.vertical_timer_id = bx_virt_timer.register_timer(this, vertical_timer_handler,
       50000, 1, 0, 0, "vertical_timer");
  }
  BX_VOODOO_THIS s.vdraw.clock_enabled = 1;
  BX_VOODOO_THIS s.vdraw.output_on = 0;
  BX_VOODOO_THIS s.vdraw.override_on = 0;
  BX_VOODOO_THIS s.vdraw.screen_update_pending = 0;
  BX_VOODOO_THIS s.vdraw.gui_update_pending = 0;

  v = new voodoo_state;
  memset(v, 0, sizeof(voodoo_state));
  BX_INIT_MUTEX(fifo_mutex);
  if (BX_VOODOO_THIS s.model >= VOODOO_2) {
    v->fbi.cmdfifo[0].depth_needed = BX_MAX_BIT32U;
    BX_INIT_MUTEX(cmdfifo_mutex);
  }

  voodoo_init(BX_VOODOO_THIS s.model);

  bx_create_event(&fifo_wakeup);
  bx_create_event(&fifo_not_full);
  bx_set_event(&fifo_not_full);
  BX_THREAD_CREATE(fifo_thread, this, fifo_thread_var);

  BX_INFO(("3dfx Voodoo Graphics adapter (model=%s) initialized",
           SIM->get_param_enum("model", base)->get_selected()));
}

void bx_voodoo_c::reset(unsigned type)
{
  unsigned i;

  static const struct reset_vals_t {
    unsigned      addr;
    unsigned char val;
  } reset_vals[] = {
    { 0x04, 0x00 }, { 0x05, 0x00 }, // command io / memory
    { 0x06, 0x00 }, { 0x07, 0x00 }, // status
    // address space 0x10 - 0x13
    { 0x11, 0x00 },
    { 0x12, 0x00 }, { 0x13, 0x00 },
    { 0x3c, 0x00 },                 // IRQ
    // initEnable
    { 0x40, 0x00 }, { 0x41, 0x00 },
    { 0x42, 0x00 }, { 0x43, 0x00 },
    // busSnoop0
    { 0x44, 0x00 }, { 0x45, 0x00 },
    { 0x46, 0x00 }, { 0x47, 0x00 },
    // busSnoop1
    { 0x48, 0x00 }, { 0x49, 0x00 },
    { 0x4a, 0x00 }, { 0x4b, 0x00 },

  };
  if (BX_VOODOO_THIS s.model != VOODOO_BANSHEE) {
    for (i = 0; i < sizeof(reset_vals) / sizeof(*reset_vals); ++i) {
      BX_VOODOO_THIS pci_conf[reset_vals[i].addr] = reset_vals[i].val;
    }
    if (BX_VOODOO_THIS s.model == VOODOO_2) {
      BX_VOODOO_THIS pci_conf[0x41] = 0x50;
      v->pci.init_enable = 0x5000;
    } else {
      v->pci.init_enable = 0x0000;
    }
  } else {
    // TODO
  }

  if ((!BX_VOODOO_THIS s.vdraw.clock_enabled || !BX_VOODOO_THIS s.vdraw.output_on) && BX_VOODOO_THIS s.vdraw.override_on) {
    mode_change_timer_handler(NULL);
  }

  // Deassert IRQ
  set_irq_level(0);
}

void bx_voodoo_c::register_state(void)
{
  char name[8];
  int i, j, k;

  bx_list_c *list = new bx_list_c(SIM->get_bochs_root(), "voodoo", "Voodoo State");
  bx_list_c *vstate = new bx_list_c(list, "vstate", "Voodoo Device State");
  new bx_shadow_data_c(vstate, "reg", (Bit8u*)v->reg, sizeof(v->reg));
  new bx_shadow_num_c(vstate, "alt_regmap", &v->alt_regmap);
  new bx_shadow_num_c(vstate, "pci_init_enable", &v->pci.init_enable, BASE_HEX);
  bx_list_c *dac = new bx_list_c(vstate, "dac", "DAC");
  for (i = 0; i < 8; i++) {
    sprintf(name, "reg%d", i);
    new bx_shadow_num_c(dac, name, &v->dac.reg[i], BASE_HEX);
  }
  new bx_shadow_num_c(dac, "read_result", &v->dac.read_result, BASE_HEX);
  new bx_shadow_num_c(dac, "clk0_freq", &v->dac.clk0_freq);
  bx_list_c *fbi = new bx_list_c(vstate, "fbi", "framebuffer");
  if ((BX_VOODOO_THIS s.model < VOODOO_BANSHEE) || (theVoodooVga == NULL)) {
    new bx_shadow_data_c(fbi, "ram", v->fbi.ram, v->fbi.mask + 1);
  }
  new bx_shadow_num_c(fbi, "rgboffs0", &v->fbi.rgboffs[0], BASE_HEX);
  new bx_shadow_num_c(fbi, "rgboffs1", &v->fbi.rgboffs[1], BASE_HEX);
  new bx_shadow_num_c(fbi, "rgboffs2", &v->fbi.rgboffs[2], BASE_HEX);
  new bx_shadow_num_c(fbi, "auxoffs", &v->fbi.auxoffs, BASE_HEX);
  new bx_shadow_num_c(fbi, "frontbuf", &v->fbi.frontbuf);
  new bx_shadow_num_c(fbi, "backbuf", &v->fbi.backbuf);
  new bx_shadow_num_c(fbi, "swaps_pending", &v->fbi.swaps_pending);
  new bx_shadow_num_c(fbi, "yorigin", &v->fbi.yorigin);
  new bx_shadow_num_c(fbi, "width", &v->fbi.width);
  new bx_shadow_num_c(fbi, "height", &v->fbi.height);
  new bx_shadow_num_c(fbi, "rowpixels", &v->fbi.rowpixels);
  new bx_shadow_num_c(fbi, "vblank", &v->fbi.vblank);
  new bx_shadow_num_c(fbi, "vblank_count", &v->fbi.vblank_count);
  new bx_shadow_bool_c(fbi, "vblank_swap_pending", &v->fbi.vblank_swap_pending);
  new bx_shadow_num_c(fbi, "vblank_swap", &v->fbi.vblank_swap);
  new bx_shadow_num_c(fbi, "vblank_dont_swap", &v->fbi.vblank_dont_swap);
  new bx_shadow_bool_c(fbi, "cheating_allowed", &v->fbi.cheating_allowed);
  new bx_shadow_num_c(fbi, "sign", &v->fbi.sign);
  new bx_shadow_num_c(fbi, "ax", &v->fbi.ax);
  new bx_shadow_num_c(fbi, "ay", &v->fbi.ay);
  new bx_shadow_num_c(fbi, "bx", &v->fbi.bx);
  new bx_shadow_num_c(fbi, "by", &v->fbi.by);
  new bx_shadow_num_c(fbi, "cx", &v->fbi.cx);
  new bx_shadow_num_c(fbi, "cy", &v->fbi.cy);
  new bx_shadow_num_c(fbi, "startr", &v->fbi.startr);
  new bx_shadow_num_c(fbi, "startg", &v->fbi.startg);
  new bx_shadow_num_c(fbi, "startb", &v->fbi.startb);
  new bx_shadow_num_c(fbi, "starta", &v->fbi.starta);
  new bx_shadow_num_c(fbi, "startz", &v->fbi.startz);
  new bx_shadow_num_c(fbi, "startw", &v->fbi.startw);
  new bx_shadow_num_c(fbi, "drdx", &v->fbi.drdx);
  new bx_shadow_num_c(fbi, "dgdx", &v->fbi.dgdx);
  new bx_shadow_num_c(fbi, "dbdx", &v->fbi.dbdx);
  new bx_shadow_num_c(fbi, "dadx", &v->fbi.dadx);
  new bx_shadow_num_c(fbi, "dzdx", &v->fbi.dzdx);
  new bx_shadow_num_c(fbi, "dwdx", &v->fbi.dwdx);
  new bx_shadow_num_c(fbi, "drdy", &v->fbi.drdy);
  new bx_shadow_num_c(fbi, "dgdy", &v->fbi.dgdy);
  new bx_shadow_num_c(fbi, "dbdy", &v->fbi.dbdy);
  new bx_shadow_num_c(fbi, "dady", &v->fbi.dady);
  new bx_shadow_num_c(fbi, "dzdy", &v->fbi.dzdy);
  new bx_shadow_num_c(fbi, "dwdy", &v->fbi.dwdy);
  new bx_shadow_num_c(fbi, "sverts", &v->fbi.sverts);
  bx_list_c *svert = new bx_list_c(fbi, "svert", "");
  for (i = 0; i < 3; i++) {
    sprintf(name, "%d", i);
    bx_list_c *num = new bx_list_c(svert, name, "");
    new bx_shadow_num_c(num, "x", &v->fbi.svert[i].x);
    new bx_shadow_num_c(num, "y", &v->fbi.svert[i].y);
    new bx_shadow_num_c(num, "a", &v->fbi.svert[i].a);
    new bx_shadow_num_c(num, "r", &v->fbi.svert[i].r);
    new bx_shadow_num_c(num, "g", &v->fbi.svert[i].g);
    new bx_shadow_num_c(num, "b", &v->fbi.svert[i].b);
    new bx_shadow_num_c(num, "z", &v->fbi.svert[i].z);
    new bx_shadow_num_c(num, "wb", &v->fbi.svert[i].wb);
    new bx_shadow_num_c(num, "w0", &v->fbi.svert[i].w0);
    new bx_shadow_num_c(num, "s0", &v->fbi.svert[i].s0);
    new bx_shadow_num_c(num, "t0", &v->fbi.svert[i].t0);
    new bx_shadow_num_c(num, "w1", &v->fbi.svert[i].w1);
    new bx_shadow_num_c(num, "s1", &v->fbi.svert[i].s1);
    new bx_shadow_num_c(num, "t1", &v->fbi.svert[i].t1);
  }
  bx_list_c *cmdfifo = new bx_list_c(fbi, "cmdfifo", "");
  for (i = 0; i < 2; i++) {
    sprintf(name, "%d", i);
    bx_list_c *num = new bx_list_c(cmdfifo, name, "");
    new bx_shadow_bool_c(num, "enabled", &v->fbi.cmdfifo[i].enabled, BASE_HEX);
    new bx_shadow_num_c(num, "base", &v->fbi.cmdfifo[i].base, BASE_HEX);
    new bx_shadow_num_c(num, "end", &v->fbi.cmdfifo[i].end, BASE_HEX);
    new bx_shadow_num_c(num, "rdptr", &v->fbi.cmdfifo[i].rdptr, BASE_HEX);
    new bx_shadow_num_c(num, "amin", &v->fbi.cmdfifo[i].amin, BASE_HEX);
    new bx_shadow_num_c(num, "amax", &v->fbi.cmdfifo[i].amax, BASE_HEX);
    new bx_shadow_num_c(num, "depth", &v->fbi.cmdfifo[i].depth);
    new bx_shadow_num_c(num, "depth_needed", &v->fbi.cmdfifo[i].depth_needed);
    new bx_shadow_bool_c(num, "cmd_ready", &v->fbi.cmdfifo[i].cmd_ready);
  }
  bx_list_c *fogblend = new bx_list_c(fbi, "fogblend", "");
  for (i = 0; i < 64; i++) {
    sprintf(name, "%d", i);
    new bx_shadow_num_c(fogblend, name, &v->fbi.fogblend[i]);
  }
  bx_list_c *fogdelta = new bx_list_c(fbi, "fogdelta", "");
  for (i = 0; i < 64; i++) {
    sprintf(name, "%d", i);
    new bx_shadow_num_c(fogdelta, name, &v->fbi.fogdelta[i]);
  }
  new bx_shadow_data_c(fbi, "clut", (Bit8u*)v->fbi.clut, sizeof(v->fbi.clut));
  new bx_shadow_bool_c(fbi, "clut_dirty", &v->fbi.clut_dirty);
  bx_list_c *tmu = new bx_list_c(vstate, "tmu", "textures");
  for (i = 0; i < MAX_TMU; i++) {
    sprintf(name, "%d", i);
    bx_list_c *num = new bx_list_c(tmu, name, "");
    if (BX_VOODOO_THIS s.model < VOODOO_BANSHEE) {
      new bx_shadow_data_c(num, "ram", v->tmu[i].ram, (4 << 20));
    }
    new bx_shadow_bool_c(num, "regdirty", &v->tmu[i].regdirty);
    new bx_shadow_num_c(num, "starts", &v->tmu[i].starts);
    new bx_shadow_num_c(num, "startt", &v->tmu[i].startt);
    new bx_shadow_num_c(num, "startw", &v->tmu[i].startw);
    new bx_shadow_num_c(num, "dsdx", &v->tmu[i].dsdx);
    new bx_shadow_num_c(num, "dtdx", &v->tmu[i].dtdx);
    new bx_shadow_num_c(num, "dwdx", &v->tmu[i].dwdx);
    new bx_shadow_num_c(num, "dsdy", &v->tmu[i].dsdy);
    new bx_shadow_num_c(num, "dtdy", &v->tmu[i].dtdy);
    new bx_shadow_num_c(num, "dwdy", &v->tmu[i].dwdy);
    new bx_shadow_num_c(num, "lodmin", &v->tmu[i].lodmin);
    new bx_shadow_num_c(num, "lodmax", &v->tmu[i].lodmax);
    new bx_shadow_num_c(num, "lodbias", &v->tmu[i].lodbias);
    new bx_shadow_num_c(num, "lodmask", &v->tmu[i].lodmask);
    bx_list_c *lodoffs = new bx_list_c(num, "lodoffset", "");
    for (j = 0; j < 9; j++) {
      sprintf(name, "%d", j);
      new bx_shadow_num_c(lodoffs, name, &v->tmu[i].lodoffset[j]);
    }
    new bx_shadow_num_c(num, "detailmax", &v->tmu[i].detailmax);
    new bx_shadow_num_c(num, "detailbias", &v->tmu[i].detailbias);
    new bx_shadow_num_c(num, "wmask", &v->tmu[i].wmask);
    new bx_shadow_num_c(num, "hmask", &v->tmu[i].hmask);
    bx_list_c *ncc = new bx_list_c(num, "ncc", "");
    for (j = 0; j < 2; j++) {
      sprintf(name, "%d", j);
      bx_list_c *ncct = new bx_list_c(ncc, name, "");
      new bx_shadow_bool_c(ncct, "dirty", &v->tmu[i].ncc[j].dirty);
      bx_list_c *ir = new bx_list_c(ncct, "ir", "");
      bx_list_c *ig = new bx_list_c(ncct, "ig", "");
      bx_list_c *ib = new bx_list_c(ncct, "ib", "");
      bx_list_c *qr = new bx_list_c(ncct, "qr", "");
      bx_list_c *qg = new bx_list_c(ncct, "qg", "");
      bx_list_c *qb = new bx_list_c(ncct, "qb", "");
      for (k = 0; k < 4; k++) {
        sprintf(name, "%d", k);
        new bx_shadow_num_c(ir, name, &v->tmu[i].ncc[j].ir[k]);
        new bx_shadow_num_c(ig, name, &v->tmu[i].ncc[j].ig[k]);
        new bx_shadow_num_c(ib, name, &v->tmu[i].ncc[j].ib[k]);
        new bx_shadow_num_c(qr, name, &v->tmu[i].ncc[j].qr[k]);
        new bx_shadow_num_c(qg, name, &v->tmu[i].ncc[j].qg[k]);
        new bx_shadow_num_c(qb, name, &v->tmu[i].ncc[j].qb[k]);
      }
      bx_list_c *y = new bx_list_c(ncct, "y", "");
      for (k = 0; k < 16; k++) {
        sprintf(name, "%d", k);
        new bx_shadow_num_c(y, name, &v->tmu[i].ncc[j].y[k]);
      }
      new bx_shadow_data_c(ncct, "texel", (Bit8u*)v->tmu[i].ncc[j].texel, 256 * sizeof(rgb_t));
    }
    new bx_shadow_data_c(num, "palette", (Bit8u*)v->tmu[i].palette, 256 * sizeof(rgb_t));
    new bx_shadow_data_c(num, "palettea", (Bit8u*)v->tmu[i].palettea, 256 * sizeof(rgb_t));
  }
  new bx_shadow_num_c(vstate, "send_config", &v->send_config);
  if (BX_VOODOO_THIS s.model == VOODOO_BANSHEE) {
    bx_list_c *banshee = new bx_list_c(list, "banshee", "Voodoo Banshee State");
    new bx_shadow_data_c(banshee, "io", (Bit8u*)v->banshee.io, 256, 1);
    new bx_shadow_data_c(banshee, "agp", (Bit8u*)v->banshee.agp, 0x80, 1);
    new bx_shadow_data_c(banshee, "crtc", (Bit8u*)v->banshee.crtc, 0x27, 1);
    new bx_shadow_data_c(banshee, "blt_reg", (Bit8u*)v->banshee.blt.reg, 0x80, 1);
    new bx_shadow_num_c(banshee, "bpp", &v->banshee.bpp);
    new bx_shadow_bool_c(banshee, "hwcursor_enabled", &v->banshee.hwcursor.enabled);
    new bx_shadow_bool_c(banshee, "hwcursor_mode", &v->banshee.hwcursor.mode);
    new bx_shadow_num_c(banshee, "hwcursor_addr", &v->banshee.hwcursor.addr, BASE_HEX);
    new bx_shadow_num_c(banshee, "hwcursor_x", &v->banshee.hwcursor.x, BASE_HEX);
    new bx_shadow_num_c(banshee, "hwcursor_y", &v->banshee.hwcursor.y, BASE_HEX);
    new bx_shadow_num_c(banshee, "hwcursor_color0", &v->banshee.hwcursor.color[0], BASE_HEX);
    new bx_shadow_num_c(banshee, "hwcursor_color1", &v->banshee.hwcursor.color[1], BASE_HEX);
    // TODO
  }
  bx_list_c *vdraw = new bx_list_c(list, "vdraw", "Voodoo Draw State");
  new bx_shadow_bool_c(vdraw, "clock_enabled", &BX_VOODOO_THIS s.vdraw.clock_enabled);
  new bx_shadow_bool_c(vdraw, "output_on", &BX_VOODOO_THIS s.vdraw.output_on);
  new bx_shadow_bool_c(vdraw, "override_on", &BX_VOODOO_THIS s.vdraw.override_on);

  register_pci_state(list);
}

void bx_voodoo_c::after_restore_state(void)
{
  if (BX_VOODOO_THIS s.model != VOODOO_BANSHEE) {
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, mem_write_handler,
                             &BX_VOODOO_THIS pci_base_address[0],
                             &BX_VOODOO_THIS pci_conf[0x10],
                             0x1000000)) {
      BX_INFO(("new mem base address: 0x%08x", BX_VOODOO_THIS pci_base_address[0]));
    }
    if (BX_VOODOO_THIS s.vdraw.override_on) {
      // force update
      v->fbi.video_changed = 1;
      BX_VOODOO_THIS s.vdraw.frame_start = bx_virt_timer.time_usec(0);
      BX_VOODOO_THIS update_timing();
      DEV_vga_set_override(1, BX_VOODOO_THIS_PTR);
    }
  } else {
    bx_pci_device_c::after_restore_pci_state(mem_read_handler);
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, mem_write_handler,
                             &BX_VOODOO_THIS pci_base_address[0],
                             &BX_VOODOO_THIS pci_conf[0x10],
                             0x2000000)) {
      BX_INFO(("new mem base address: 0x%08x", BX_VOODOO_THIS pci_base_address[0]));
    }
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, mem_write_handler,
                             &BX_VOODOO_THIS pci_base_address[1],
                             &BX_VOODOO_THIS pci_conf[0x14],
                             0x2000000)) {
      BX_INFO(("new lfb base address: 0x%08x", BX_VOODOO_THIS pci_base_address[1]));
    }
    if (DEV_pci_set_base_io(BX_VOODOO_THIS_PTR, banshee_read_handler, banshee_write_handler,
                            &BX_VOODOO_THIS pci_base_address[2],
                            &BX_VOODOO_THIS pci_conf[0x18],
                            256, &banshee_iomask[0], "banshee")) {
      BX_INFO(("new i/o base address: 0x%04x", BX_VOODOO_THIS pci_base_address[2]));
    }
    if ((v->banshee.io[io_vidProcCfg] & 0x01) && (theVoodooVga != NULL)) {
      theVoodooVga->banshee_update_mode();
    }
    // TODO
  }
}

bx_bool bx_voodoo_c::mem_read_handler(bx_phy_address addr, unsigned len,
                                      void *data, void *param)
{
  Bit32u *data_ptr = (Bit32u*)data;

  if (BX_VOODOO_THIS s.model != VOODOO_BANSHEE) {
    *data_ptr = voodoo_r((addr>>2) & 0x3FFFFF);
  } else {
    BX_VOODOO_THIS banshee_mem_read(addr, len, data);
  }
  return 1;
}

bx_bool bx_voodoo_c::mem_write_handler(bx_phy_address addr, unsigned len,
                                       void *data, void *param)
{
  Bit32u val = *(Bit32u*)data;

  if (BX_VOODOO_THIS s.model != VOODOO_BANSHEE) {
    if (len == 4) {
      voodoo_w((addr>>2) & 0x3FFFFF, val, 0xffffffff);
    } else if (len == 2) {
      if (addr & 3) {
        voodoo_w((addr>>2) & 0x3FFFFF, val<<16, 0xffff0000);
      } else {
        voodoo_w((addr>>2) & 0x3FFFFF, val, 0x0000ffff);
      }
    }
  } else {
    BX_VOODOO_THIS banshee_mem_write(addr, len, data);
  }
  return 1;
}

void bx_voodoo_c::mode_change_timer_handler(void *this_ptr)
{
  UNUSED(this_ptr);
  BX_VOODOO_THIS s.vdraw.screen_update_pending = 0;

  if ((!BX_VOODOO_THIS s.vdraw.clock_enabled || !BX_VOODOO_THIS s.vdraw.output_on) && BX_VOODOO_THIS s.vdraw.override_on) {
    // switching off
    bx_virt_timer.deactivate_timer(BX_VOODOO_THIS s.vertical_timer_id);
    DEV_vga_set_override(0, NULL);
    BX_VOODOO_THIS s.vdraw.override_on = 0;
    BX_VOODOO_THIS s.vdraw.width = 0;
    BX_VOODOO_THIS s.vdraw.height = 0;
    BX_INFO(("Voodoo output disabled"));
  }

  if ((BX_VOODOO_THIS s.vdraw.clock_enabled && BX_VOODOO_THIS s.vdraw.output_on) && !BX_VOODOO_THIS s.vdraw.override_on) {
    // switching on
    if (!BX_VOODOO_THIS update_timing())
      return;
    DEV_vga_set_override(1, BX_VOODOO_THIS_PTR);
    BX_VOODOO_THIS s.vdraw.override_on = 1;
  }
}

bx_bool bx_voodoo_c::update_timing(void)
{
  int htotal, vtotal, hsync, vsync;

  if (!BX_VOODOO_THIS s.vdraw.clock_enabled || !BX_VOODOO_THIS s.vdraw.output_on)
    return 0;
  if ((v->reg[hSync].u == 0) || (v->reg[vSync].u == 0))
    return 0;
  if (BX_VOODOO_THIS s.model == VOODOO_2) {
    htotal = ((v->reg[hSync].u >> 16) & 0x7ff) + 1 + (v->reg[hSync].u & 0x1ff) + 1;
    vtotal = ((v->reg[vSync].u >> 16) & 0x1fff) + (v->reg[vSync].u & 0x1fff);
    hsync = ((v->reg[hSync].u >> 16) & 0x7ff);
    vsync = ((v->reg[vSync].u >> 16) & 0x1fff);
  } else {
    htotal = ((v->reg[hSync].u >> 16) & 0x3ff) + 1 + (v->reg[hSync].u & 0xff) + 1;
    vtotal = ((v->reg[vSync].u >> 16) & 0xfff) + (v->reg[vSync].u & 0xfff);
    hsync = ((v->reg[hSync].u >> 16) & 0x3ff);
    vsync = ((v->reg[vSync].u >> 16) & 0xfff);
  }
  float hfreq = v->dac.clk0_freq / (float)htotal;
  if (((v->reg[fbiInit1].u >> 20) & 3) == 1) { // VCLK div 2
    hfreq /= 2;
  }
  float vfreq = hfreq / (float)vtotal;
  BX_VOODOO_THIS s.vdraw.htotal_usec = (unsigned)(1000000.0 / hfreq);
  BX_VOODOO_THIS s.vdraw.vtotal_usec = (unsigned)(1000000.0 / vfreq);
  BX_VOODOO_THIS s.vdraw.htime_to_pixel = (double)htotal / (1000000.0 / hfreq);
  BX_VOODOO_THIS s.vdraw.hsync_usec = BX_VOODOO_THIS s.vdraw.htotal_usec * hsync / htotal;
  BX_VOODOO_THIS s.vdraw.vsync_usec = vsync * BX_VOODOO_THIS s.vdraw.htotal_usec;
  if ((BX_VOODOO_THIS s.vdraw.width != v->fbi.width) ||
      (BX_VOODOO_THIS s.vdraw.height != v->fbi.height)) {
    BX_VOODOO_THIS s.vdraw.width = v->fbi.width;
    BX_VOODOO_THIS s.vdraw.height = v->fbi.height;
    bx_gui->dimension_update(v->fbi.width, v->fbi.height, 0, 0, 16);
    vertical_timer_handler(NULL);
  }
  BX_INFO(("Voodoo output %dx%d@%uHz", v->fbi.width, v->fbi.height, (unsigned)vfreq));
  bx_virt_timer.activate_timer(BX_VOODOO_THIS s.vertical_timer_id, (Bit32u)BX_VOODOO_THIS s.vdraw.vtotal_usec, 1);
  return 1;
}

void bx_voodoo_c::refresh_display(void *this_ptr, bx_bool redraw)
{
  if (redraw) {
    redraw_area(0, 0, v->fbi.width, v->fbi.height);
  }
  vertical_timer_handler(this_ptr);
  update();
}

void bx_voodoo_c::vertical_timer_handler(void *this_ptr)
{
  UNUSED(this_ptr);

  BX_VOODOO_THIS s.vdraw.frame_start = bx_virt_timer.time_usec(0);

  BX_LOCK(fifo_mutex);
  if (!fifo_empty(&v->pci.fifo) || !fifo_empty(&v->fbi.fifo)) {
    bx_set_event(&fifo_wakeup);
  }
  BX_UNLOCK(fifo_mutex);
  if (v->fbi.cmdfifo[0].cmd_ready) {
    bx_set_event(&fifo_wakeup);
  }

  if (v->fbi.vblank_swap_pending) {
    swap_buffers(v);
  }

  rectangle re;
  re.min_x = re.min_y = 0;
  re.max_x = v->fbi.width;
  re.max_y = v->fbi.height;
  if (!voodoo_update(&re))
    return;
  BX_VOODOO_THIS s.vdraw.gui_update_pending = 1;
}

void bx_voodoo_c::update(void)
{
  unsigned pitch;
  unsigned xc, yc, xti, yti;
  unsigned r, c, w, h;
  int i;
  unsigned long colour;
  Bit8u * vid_ptr, * vid_ptr2;
  Bit8u * tile_ptr, * tile_ptr2;
  bx_svga_tileinfo_t info;

  if (!BX_VOODOO_THIS s.vdraw.gui_update_pending)
    return;

  Bit8u *disp_ptr = (Bit8u*)(v->fbi.ram + v->fbi.rgboffs[v->fbi.frontbuf]);
  pitch = v->fbi.rowpixels * 2;

  if (bx_gui->graphics_tile_info_common(&info)) {
    if (info.snapshot_mode) {
      vid_ptr = disp_ptr;
      tile_ptr = bx_gui->get_snapshot_buffer();
      if (tile_ptr != NULL) {
        for (yc = 0; yc < BX_VOODOO_THIS s.vdraw.height; yc++) {
          memcpy(tile_ptr, vid_ptr, info.pitch);
          vid_ptr += pitch;
          tile_ptr += info.pitch;
        }
      }
    } else if (info.is_indexed) {
      BX_ERROR(("current guest pixel format is unsupported on indexed colour host displays"));
    } else {
      for (yc=0, yti = 0; yc<BX_VOODOO_THIS s.vdraw.height; yc+=Y_TILESIZE, yti++) {
        for (xc=0, xti = 0; xc<BX_VOODOO_THIS s.vdraw.width; xc+=X_TILESIZE, xti++) {
          vid_ptr = disp_ptr + (yc * pitch + (xc<<1));
          tile_ptr = bx_gui->graphics_tile_get(xc, yc, &w, &h);
          for (r=0; r<h; r++) {
            vid_ptr2  = vid_ptr;
            tile_ptr2 = tile_ptr;
            for (c=0; c<w; c++) {
              colour = *(vid_ptr2++);
              colour |= *(vid_ptr2++) << 8;
              colour = MAKE_COLOUR(
                colour & 0x001f, 5, info.blue_shift, info.blue_mask,
                colour & 0x07e0, 11, info.green_shift, info.green_mask,
                colour & 0xf800, 16, info.red_shift, info.red_mask);
              if (info.is_little_endian) {
                for (i=0; i<info.bpp; i+=8) {
                  *(tile_ptr2++) = (Bit8u)(colour >> i);
                }
              } else {
                for (i=info.bpp-8; i>-8; i-=8) {
                  *(tile_ptr2++) = (Bit8u)(colour >> i);
                }
              }
            }
            vid_ptr  += pitch;
            tile_ptr += info.pitch;
          }
          bx_gui->graphics_tile_update_in_place(xc, yc, w, h);
        }
      }
    }
  } else {
    BX_PANIC(("cannot get svga tile info"));
  }
  BX_VOODOO_THIS s.vdraw.gui_update_pending = 0;
}

void bx_voodoo_c::redraw_area(unsigned x0, unsigned y0, unsigned width,
                      unsigned height)
{
  // TODO: implement tile-based update mechanism
  v->fbi.video_changed = 1;
}

Bit32u bx_voodoo_c::get_retrace(bx_bool hv)
{
  Bit64u time_in_frame = bx_virt_timer.time_usec(0) - BX_VOODOO_THIS s.vdraw.frame_start;
  if (time_in_frame >= BX_VOODOO_THIS s.vdraw.vsync_usec) {
    return 0;
  } else {
    Bit32u value = (Bit32u)(time_in_frame / BX_VOODOO_THIS s.vdraw.htotal_usec + 1);
    if (hv) {
      Bit32u time_in_line = (Bit32u)(time_in_frame % BX_VOODOO_THIS s.vdraw.htotal_usec);
      Bit32u hpixel = (Bit32u)(time_in_line * BX_VOODOO_THIS s.vdraw.htime_to_pixel);
      if (time_in_line < BX_VOODOO_THIS s.vdraw.hsync_usec) {
        value |= ((hpixel + 1) << 16);
      }
    }
    return value;
  }
}

void bx_voodoo_c::output_enable(bx_bool enabled)
{
  if (BX_VOODOO_THIS s.vdraw.output_on != enabled) {
    BX_VOODOO_THIS s.vdraw.output_on = enabled;
    update_screen_start();
  }
}

void bx_voodoo_c::update_screen_start(void)
{
  if (!BX_VOODOO_THIS s.vdraw.screen_update_pending) {
    BX_VOODOO_THIS s.vdraw.screen_update_pending = 1;
    bx_virt_timer.activate_timer(BX_VOODOO_THIS s.mode_change_timer_id, 1000, 0);
  }
}

void bx_voodoo_c::set_irq_level(bx_bool level)
{
  DEV_pci_set_irq(BX_VOODOO_THIS s.devfunc, BX_VOODOO_THIS pci_conf[0x3d], level);
}


// pci configuration space write callback handler
void bx_voodoo_c::pci_write_handler(Bit8u address, Bit32u value, unsigned io_len)
{
  Bit8u value8, oldval;
  bx_bool baseaddr_change = 0;

  if (BX_VOODOO_THIS s.model == VOODOO_BANSHEE) {
    banshee_pci_write_handler(address, value, io_len);
    return;
  }

  if ((address >= 0x14) && (address < 0x34))
    return;

  for (unsigned i=0; i<io_len; i++) {
    value8 = (value >> (i*8)) & 0xFF;
    oldval = BX_VOODOO_THIS pci_conf[address+i];
    switch (address+i) {
      case 0x04:
        value8 &= 0x02;
        break;
      case 0x3c:
        if (value8 != oldval) {
          BX_INFO(("new irq line = %d", value8));
        }
        break;
      case 0x10:
        value8 = (value8 & 0xf0) | (oldval & 0x0f);
      case 0x11:
      case 0x12:
      case 0x13:
        baseaddr_change |= (value8 != oldval);
        break;
      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43:
        if (((address+i) == 0x40) && ((value8 ^ oldval) & 0x02)) {
          v->pci.fifo.enabled = ((value8 & 0x02) > 0);
          if (!v->pci.fifo.enabled && !fifo_empty(&v->pci.fifo)) {
            bx_set_event(&fifo_wakeup);
          }
          BX_DEBUG(("PCI FIFO now %sabled", v->pci.fifo.enabled ? "en":"dis"));
        }
        if (((address+i) == 0x41) && (BX_VOODOO_THIS s.model == VOODOO_2)) {
          value8 &= 0x0f;
          value8 |= 0x50;
        }
        v->pci.init_enable &= ~(0xff << (i*8));
        v->pci.init_enable |= (value8 << (i*8));
        break;
      case 0xc0:
        BX_VOODOO_THIS s.vdraw.clock_enabled = 1;
        update_screen_start();
        break;
      case 0xe0:
        BX_VOODOO_THIS s.vdraw.clock_enabled = 0;
        update_screen_start();
        break;
      default:
        value8 = oldval;
    }
    BX_VOODOO_THIS pci_conf[address+i] = value8;
  }
  if (baseaddr_change) {
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, mem_write_handler,
                             &BX_VOODOO_THIS pci_base_address[0],
                             &BX_VOODOO_THIS pci_conf[0x10],
                             0x1000000)) {
      BX_INFO(("new mem base address: 0x%08x", BX_VOODOO_THIS pci_base_address[0]));
    }
  }

  if (io_len == 1)
    BX_DEBUG(("write PCI register 0x%02x value 0x%02x", address, value));
  else if (io_len == 2)
    BX_DEBUG(("write PCI register 0x%02x value 0x%04x", address, value));
  else if (io_len == 4)
    BX_DEBUG(("write PCI register 0x%02x value 0x%08x", address, value));
}

// Banshee pci configuration space write handler
void bx_voodoo_c::banshee_pci_write_handler(Bit8u address, Bit32u value, unsigned io_len)
{
  Bit8u value8, oldval;
  bx_bool baseaddr0_change = 0, baseaddr1_change = 0, baseaddr2_change = 0;
  bx_bool romaddr_change = 0;

  if ((address >= 0x1c) && (address < 0x30))
    return;

  for (unsigned i=0; i<io_len; i++) {
    value8 = (value >> (i*8)) & 0xFF;
    oldval = BX_VOODOO_THIS pci_conf[address+i];
    switch (address+i) {
      case 0x04:
        value8 &= 0x03;
        break;
      case 0x3c:
        if (value8 != oldval) {
          BX_INFO(("new irq line = %d", value8));
        }
        break;
      case 0x10:
        value8 = (value8 & 0xf0) | (oldval & 0x0f);
      case 0x11:
      case 0x12:
      case 0x13:
        baseaddr0_change |= (value8 != oldval);
        break;
      case 0x14:
        value8 = (value8 & 0xf0) | (oldval & 0x0f);
      case 0x15:
      case 0x16:
      case 0x17:
        baseaddr1_change |= (value8 != oldval);
        break;
      case 0x18:
        value8 = (value8 & 0xf0) | (oldval & 0x0f);
      case 0x19:
      case 0x1a:
      case 0x1b:
        baseaddr2_change |= (value8 != oldval);
        break;
      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33:
        if (BX_VOODOO_THIS pci_rom_size > 0) {
          if ((address+i) == 0x30) {
            value8 &= 0x01;
          } else if ((address+i) == 0x31) {
            value8 &= 0xfc;
          }
          romaddr_change = 1;
        }
        break;
      default:
        if (address >= 0x54) {
          value8 = oldval;
        }
    }
    BX_VOODOO_THIS pci_conf[address+i] = value8;
  }
  if (baseaddr0_change) {
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, mem_write_handler,
                             &BX_VOODOO_THIS pci_base_address[0],
                             &BX_VOODOO_THIS pci_conf[0x10],
                             0x2000000)) {
      BX_INFO(("new mem base address: 0x%08x", BX_VOODOO_THIS pci_base_address[0]));
    }
  }
  if (baseaddr1_change) {
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, mem_write_handler,
                             &BX_VOODOO_THIS pci_base_address[1],
                             &BX_VOODOO_THIS pci_conf[0x14],
                             0x2000000)) {
      BX_INFO(("new lfb base address: 0x%08x", BX_VOODOO_THIS pci_base_address[1]));
    }
  }
  if (baseaddr2_change) {
    if (DEV_pci_set_base_io(BX_VOODOO_THIS_PTR, banshee_read_handler, banshee_write_handler,
                            &BX_VOODOO_THIS pci_base_address[2],
                            &BX_VOODOO_THIS pci_conf[0x18],
                            256, &banshee_iomask[0], "banshee")) {
      BX_INFO(("new i/o base address: 0x%04x", BX_VOODOO_THIS pci_base_address[2]));
    }
  }
  if (romaddr_change) {
    if (DEV_pci_set_base_mem(BX_VOODOO_THIS_PTR, mem_read_handler, NULL,
                             &BX_VOODOO_THIS pci_rom_address,
                             &BX_VOODOO_THIS pci_conf[0x30],
                             BX_VOODOO_THIS pci_rom_size)) {
      BX_INFO(("new ROM address: 0x%08x", BX_VOODOO_THIS pci_rom_address));
    }
  }

  if (io_len == 1)
    BX_DEBUG(("write PCI register 0x%02x value 0x%02x", address, value));
  else if (io_len == 2)
    BX_DEBUG(("write PCI register 0x%02x value 0x%04x", address, value));
  else if (io_len == 4)
    BX_DEBUG(("write PCI register 0x%02x value 0x%08x", address, value));
}

Bit32u bx_voodoo_c::banshee_read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
  UNUSED(this_ptr);

  Bit32u result;

  Bit8u offset = (Bit8u)(address & 0xff);
  Bit8u reg = (offset>>2);
  BX_DEBUG(("banshee read from offset 0x%02x (%s)", offset, banshee_io_reg_name[reg]));
  switch (offset>>2) {
    case io_status:
      result = register_r(0);
      break;

    case io_dacData:
      result = v->fbi.clut[v->banshee.io[io_dacAddr] & 0x1ff] = v->banshee.io[reg];
      break;

    case io_vgab0:  case io_vgab4:  case io_vgab8:  case io_vgabc:
    case io_vgac0:  case io_vgac4:  case io_vgac8:  case io_vgacc:
    case io_vgad0:  case io_vgad4:  case io_vgad8:  case io_vgadc:
      result = 0;
      if (theVoodooVga != NULL) {
        for (unsigned i=0; i<io_len; i++) {
          result |= (theVoodooVga->banshee_vga_read_handler(theVoodooVga, 0x300+offset+i, 1) << (i*8));
        }
      }
      break;

    case io_vidSerialParallelPort:
      result = v->banshee.io[reg] | 0x0c600000;
      break;

    default:
      result = v->banshee.io[reg];
      break;
  }
  BX_DEBUG(("banshee read result = 0x%08x", result));
  return result;
}

void bx_voodoo_c::banshee_write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
  UNUSED(this_ptr);

  Bit8u offset = (Bit8u)(address & 0xff);
  Bit8u reg = (offset>>2), dac_idx, k, m, n;
  Bit32u old = v->banshee.io[reg];
  double vfreq;
  bx_bool prev_hwce = v->banshee.hwcursor.enabled;
  Bit16u prev_hwcx = v->banshee.hwcursor.x;
  Bit16u prev_hwcy = v->banshee.hwcursor.y;

  BX_DEBUG(("banshee write to offset 0x%02x: value = 0x%08x len=%d (%s)", offset, value,
            io_len, banshee_io_reg_name[reg]));
  switch (reg) {
    case io_lfbMemoryConfig:
      v->banshee.io[reg] = value;
      v->fbi.lfb_base = (value & 0x1fff) << 13;
      v->fbi.lfb_stride = ((value >> 13) & 7) + 10;
      break;

    case io_miscInit0:
      v->banshee.io[reg] = value;
      v->fbi.yorigin = (value >> 18) & 0xfff;
      break;

    case io_vgaInit0:
      v->banshee.io[reg] = value;
      if (theVoodooVga != NULL) {
        theVoodooVga->banshee_set_dac_mode((v->banshee.io[reg] & 0x04) != 0);
      }
      break;

    case io_dramCommand:
      BX_VOODOO_THIS banshee_blt_reg_write(0x1c, value);
      break;

    case io_dramData:
      BX_VOODOO_THIS banshee_blt_reg_write(0x19, value);
      break;

    case io_strapInfo:
      break;

    case io_pllCtrl0:
      if (value != v->banshee.io[reg]) {
        v->banshee.io[reg] = value;
        k = (Bit8u)(value & 0x03);
        m = (Bit8u)((value >> 2) & 0x3f);
        n = (Bit8u)((value >> 8) & 0xff);
        vfreq = 14318180.0f * ((double)n + 2.0f) / ((double)m + 2.0f) / (double)(1 << k);
        BX_INFO(("Setting VCLK #3 (pllCtrl0) = %.3f MHz", vfreq / 1000000.0f));
        if (theVoodooVga != NULL) {
          theVoodooVga->banshee_set_vclk3((Bit32u)vfreq);
        }
      }
      break;

    case io_dacData:
      v->banshee.io[reg] = value;
      if (v->banshee.io[reg] != v->fbi.clut[v->banshee.io[io_dacAddr] & 0x1ff]) {
        v->fbi.clut[v->banshee.io[io_dacAddr] & 0x1ff] = v->banshee.io[reg];
        v->fbi.clut_dirty = 1;
        dac_idx = v->banshee.io[io_dacAddr] & 0xff;
        bx_gui->palette_change_common(dac_idx, (v->fbi.clut[dac_idx] >> 16) & 0xff,
                                      (v->fbi.clut[dac_idx] >> 8) & 0xff,
                                      v->fbi.clut[dac_idx] & 0xff);
      }
      break;

    case io_vidProcCfg:
      v->banshee.io[reg] = value;
      if ((v->banshee.io[reg] ^ old) & 0x2800)
        v->fbi.clut_dirty = 1;
      if ((v->banshee.io[reg] & 0x01) && ((old & 0x01) == 0x00)) {
        if (theVoodooVga != NULL) {
          theVoodooVga->banshee_update_mode();
        }
      }
      v->banshee.hwcursor.enabled = ((v->banshee.io[reg] >> 27) & 1);
      v->banshee.hwcursor.mode = ((v->banshee.io[reg] >> 1) & 1);
      if (v->banshee.hwcursor.enabled != prev_hwce) {
        theVoodooVga->redraw_area(v->banshee.hwcursor.x - 63, v->banshee.hwcursor.y - 63,
                                  v->banshee.hwcursor.x, v->banshee.hwcursor.y);
      }
      break;

    case io_hwCurPatAddr:
      v->banshee.io[reg] = value;
      v->banshee.hwcursor.addr = v->banshee.io[reg] & 0xffffff;
      if (v->banshee.hwcursor.enabled && (value != old)) {
        theVoodooVga->redraw_area(v->banshee.hwcursor.x - 63, v->banshee.hwcursor.y - 63,
                                  v->banshee.hwcursor.x, v->banshee.hwcursor.y);
      }
      break;

    case io_hwCurLoc:
      v->banshee.io[reg] = value;
      v->banshee.hwcursor.x = v->banshee.io[reg] & 0x7ff;
      v->banshee.hwcursor.y = (v->banshee.io[reg] >> 16) & 0x7ff;
      if (v->banshee.hwcursor.enabled && (value != old)) {
        theVoodooVga->redraw_area(prev_hwcx - 63, prev_hwcy - 63, prev_hwcx, prev_hwcy);
        theVoodooVga->redraw_area(v->banshee.hwcursor.x - 63, v->banshee.hwcursor.y - 63,
                                  v->banshee.hwcursor.x, v->banshee.hwcursor.y);
      }
      break;

    case io_hwCurC0:
      v->banshee.io[reg] = value;
      v->banshee.hwcursor.color[0] = v->banshee.io[reg] & 0xffffff;
      break;

    case io_hwCurC1:
      v->banshee.io[reg] = value;
      v->banshee.hwcursor.color[1] = v->banshee.io[reg] & 0xffffff;
      break;

    case io_vidScreenSize:
      v->banshee.io[reg] = value;
      v->fbi.width = (value & 0xfff);
      v->fbi.height = (value >> 12) & 0xfff;
      break;

    case io_vgab0:  case io_vgab4:  case io_vgab8:  case io_vgabc:
    case io_vgac0:  case io_vgac4:  case io_vgac8:  case io_vgacc:
    case io_vgad0:  case io_vgad4:  case io_vgad8:  case io_vgadc:
      if (theVoodooVga != NULL) {
        for (unsigned i=0; i<io_len; i++) {
          Bit8u value8 = (value >> (i*8)) & 0xff;
          theVoodooVga->banshee_vga_write_handler(theVoodooVga, 0x300+offset+i, value8, 1);
        }
      }
      break;

    default:
      v->banshee.io[reg] = value;
      break;
  }
}

void bx_voodoo_c::banshee_mem_read(bx_phy_address addr, unsigned len, void *data)
{
  Bit32u *data_ptr = (Bit32u*)data;
  Bit32u value = 0xffffffff;
  Bit32u offset = (addr & 0x1ffffff);

  if (BX_VOODOO_THIS pci_rom_size > 0) {
    Bit32u mask = (BX_VOODOO_THIS pci_rom_size - 1);
    if ((addr & ~mask) == BX_VOODOO_THIS pci_rom_address) {
      if (BX_VOODOO_THIS pci_conf[0x30] & 0x01) {
        value = 0;
        for (unsigned i = 0; i < len; i++) {
          value <<= 8;
          value |= BX_VOODOO_THIS pci_rom[(addr & mask) + i];
        }
      }
      *data_ptr = value;
      return;
    }
  }
  if ((addr & ~0x1ffffff) == BX_VOODOO_THIS pci_base_address[0]) {
    if (offset < 0x80000) {
      value = BX_VOODOO_THIS banshee_read_handler(BX_VOODOO_THIS_PTR, offset, len);
    } else if (offset < 0x100000) {
      value = banshee_agp_reg_read((offset >> 2) & 0x7f);
    } else if (offset < 0x200000) {
      value = banshee_blt_reg_read((offset >> 2) & 0x7f);
    } else if (offset < 0x600000) {
      value = register_r((offset & 0x1fffff) >> 2);
    } else if (offset < 0xc00000) {
      BX_ERROR(("reserved read from offset 0x%08x", offset));
    } else if (offset < 0x1000000) {
      BX_INFO(("TODO: YUV planar space read from offset 0x%08x", offset));
    } else {
      Bit8u temp = v->fbi.lfb_stride;
      v->fbi.lfb_stride = 11;
      value = lfb_r((offset & 0xffffff) >> 2);
      v->fbi.lfb_stride = temp;
    }
  } else if ((addr & ~0x1ffffff) == BX_VOODOO_THIS pci_base_address[1]) {
    if (offset < v->fbi.lfb_base) {
      if (offset <= v->fbi.mask) {
        value = 0;
        for (unsigned i = 0; i < len; i++) {
          value |= (v->fbi.ram[offset + i] << (i*8));
        }
      }
    } else {
      value = lfb_r((offset - v->fbi.lfb_base) >> 2);
    }
  }
  *data_ptr = value;
}

void bx_voodoo_c::banshee_mem_write(bx_phy_address addr, unsigned len, void *data)
{
  Bit32u offset = (addr & 0x1ffffff);
  Bit32u value = *(Bit32u*)data;
  Bit8u value8;
  Bit32u start = v->banshee.io[io_vidDesktopStartAddr];
  Bit32u pitch = v->banshee.io[io_vidDesktopOverlayStride] & 0x7fff;
  unsigned x, y;

  if ((addr & ~0x1ffffff) == BX_VOODOO_THIS pci_base_address[0]) {
    if (offset < 0x80000) {
      BX_VOODOO_THIS banshee_write_handler(BX_VOODOO_THIS_PTR, offset, value, len);
    } else if (offset < 0x100000) {
      banshee_agp_reg_write((offset >> 2) & 0x7f, value);
    } else if (offset < 0x200000) {
      banshee_blt_reg_write((offset >> 2) & 0x7f, value);
    } else if (offset < 0x600000) {
      register_w_common((offset & 0x1fffff) >> 2, value);
    } else if (offset < 0x800000) {
      texture_w((offset & 0x1fffff) >> 2, value);
    } else if (offset < 0xc00000) {
      BX_ERROR(("reserved write to offset 0x%08x", offset));
    } else if (offset < 0x1000000) {
      BX_INFO(("TODO: YUV planar space write to offset 0x%08x", offset));
    } else {
      Bit8u temp = v->fbi.lfb_stride;
      v->fbi.lfb_stride = 11;
      lfb_w((offset & 0xffffff) >> 2, value, 0xffffffff);
      v->fbi.lfb_stride = temp;
    }
  } else if ((addr & ~0x1ffffff) == BX_VOODOO_THIS pci_base_address[1]) {
    offset &= v->fbi.mask;
    if (offset < v->fbi.lfb_base) {
      if (v->fbi.cmdfifo[0].enabled && (offset >= v->fbi.cmdfifo[0].base) &&
          (offset < v->fbi.cmdfifo[0].end)) {
        cmdfifo_w(offset, value);
      } else if (v->fbi.cmdfifo[1].enabled && (offset >= v->fbi.cmdfifo[1].base) &&
          (offset < v->fbi.cmdfifo[1].end)) {
        BX_INFO(("TODO: CMDFIFO #1 write"));
      } else {
        for (unsigned i = 0; i < len; i++) {
          value8 = (value >> (i*8)) & 0xff;
          v->fbi.ram[offset + i] = value8;
        }
        if (offset >= start) {
          x = (offset % pitch) / (v->banshee.bpp >> 3);
          y = offset / pitch;
          theVoodooVga->redraw_area(x, y, len / (v->banshee.bpp >> 3), 1);
        }
      }
    } else {
      lfb_w((offset - v->fbi.lfb_base) >> 2, value, 0xffffffff);
    }
  }
}

Bit32u bx_voodoo_c::banshee_agp_reg_read(Bit8u reg)
{
  Bit32u result = 0;
  Bit8u fifo_idx = (reg >= cmdBaseAddr1);

  switch (reg) {
    case cmdBaseAddr0:
    case cmdBaseAddr1:
      result = (v->fbi.cmdfifo[fifo_idx].base >> 12);
      break;
    case cmdBump0:
    case cmdBump1:
      break;
    case cmdRdPtrL0:
    case cmdRdPtrL1:
      result = v->fbi.cmdfifo[fifo_idx].rdptr;
      break;
    case cmdFifoDepth0:
    case cmdFifoDepth1:
      result = v->fbi.cmdfifo[fifo_idx].depth;
      break;
    default:
      result = v->banshee.agp[reg];
  }
  BX_DEBUG(("AGP read register 0x%03x (%s) result = 0x%08x", reg<<2, 
            banshee_agp_reg_name[reg], result));
  return result;
}

void bx_voodoo_c::banshee_agp_reg_write(Bit8u reg, Bit32u value)
{
  Bit8u fifo_idx = (reg >= cmdBaseAddr1);

  BX_DEBUG(("AGP write register 0x%03x (%s) value = 0x%08x", reg<<2,
            banshee_agp_reg_name[reg], value));
  switch (reg) {
    case cmdBaseAddr0:
    case cmdBaseAddr1:
      BX_LOCK(cmdfifo_mutex);
      v->fbi.cmdfifo[fifo_idx].base = (value << 12);
      if (fifo_idx == 0) {
        v->fbi.cmdfifo[0].end = v->fbi.cmdfifo[0].base +
          (((v->banshee.agp[cmdBaseSize0] & 0xff) + 1) << 12);
      } else {
        v->fbi.cmdfifo[1].end = v->fbi.cmdfifo[1].base +
          (((v->banshee.agp[cmdBaseSize1] & 0xff) + 1) << 12);
      }
      BX_UNLOCK(cmdfifo_mutex);
      break;
    case cmdBaseSize0:
    case cmdBaseSize1:
      BX_LOCK(cmdfifo_mutex);
      if (fifo_idx == 0) {
        v->fbi.cmdfifo[0].end = v->fbi.cmdfifo[0].base + (((value & 0xff) + 1) << 12);
      } else {
        v->fbi.cmdfifo[1].end = v->fbi.cmdfifo[1].base + (((value & 0xff) + 1) << 12);
      }
      v->fbi.cmdfifo[fifo_idx].enabled = ((value >> 8) & 1);
      BX_UNLOCK(cmdfifo_mutex);
      break;
    case cmdBump0:
    case cmdBump1:
      if (value > 0) {
        BX_ERROR(("cmdBump0 not supported yet"));
      }
      break;
    case cmdRdPtrL0:
    case cmdRdPtrL1:
      BX_LOCK(cmdfifo_mutex);
      v->fbi.cmdfifo[fifo_idx].rdptr = value;
      BX_UNLOCK(cmdfifo_mutex);
      break;
    case cmdRdPtrH0:
    case cmdRdPtrH1:
      if (value > 0) {
        BX_ERROR(("cmdRdPtrH%d not supported yet", fifo_idx));
      }
      break;
    case cmdAMin0:
    case cmdAMin1:
      v->fbi.cmdfifo[fifo_idx].amin = value;
      break;
    case cmdAMax0:
    case cmdAMax1:
      v->fbi.cmdfifo[fifo_idx].amax = value;
      break;
    case cmdFifoDepth0:
    case cmdFifoDepth1:
      BX_LOCK(cmdfifo_mutex);
      v->fbi.cmdfifo[fifo_idx].depth = value & 0xfffff;
      BX_UNLOCK(cmdfifo_mutex);
      break;
    case cmdHoleCnt0:
    case cmdHoleCnt1:
      if (value > 0) {
        BX_ERROR(("cmdHoleCnt%d not supported yet", fifo_idx));
      }
      break;
  }
  v->banshee.agp[reg] = value;
}

Bit32u bx_voodoo_c::banshee_blt_reg_read(Bit8u reg)
{
  Bit32u result = 0;

  switch (reg) {
    case blt_status:
      result = register_r(0);
      break;
    default:
      result = v->banshee.blt.reg[reg];
  }
  if (reg < 0x20) {
    BX_DEBUG(("2D read register 0x%03x (%s) result = 0x%08x", reg<<2,
              banshee_blt_reg_name[reg], result));
  }
  return result;
}

void bx_voodoo_c::banshee_blt_reg_write(Bit8u reg, Bit32u value)
{
  Bit32u pbytes;
  Bit8u sfmt;

  if (reg < 0x20) {
    BX_DEBUG(("2D write register 0x%03x (%s) value = 0x%08x", reg<<2,
              banshee_blt_reg_name[reg], value));
  }
  v->banshee.blt.reg[reg] = value;
  switch (reg) {
    case blt_intrCtrl:
      BX_ERROR(("intrCtrl register not supported yet"));
      break;
    case blt_clip0Min:
      v->banshee.blt.clipx0[0] = v->banshee.blt.reg[reg] & 0xfff;
      v->banshee.blt.clipy0[0] = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_clip0Max:
      v->banshee.blt.clipx1[0] = v->banshee.blt.reg[reg] & 0xfff;
      v->banshee.blt.clipy1[0] = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_pattern0Alias:
      v->banshee.blt.reg[0x40] = value;
      break;
    case blt_pattern1Alias:
      v->banshee.blt.reg[0x41] = value;
      break;
    case blt_clip1Min:
      v->banshee.blt.clipx0[1] = v->banshee.blt.reg[reg] & 0xfff;
      v->banshee.blt.clipy0[1] = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_clip1Max:
      v->banshee.blt.clipx1[1] = v->banshee.blt.reg[reg] & 0xfff;
      v->banshee.blt.clipy1[1] = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_srcSize:
      v->banshee.blt.src_w = v->banshee.blt.reg[reg] & 0x1fff;
      v->banshee.blt.src_h = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_srcXY:
      v->banshee.blt.src_x = v->banshee.blt.reg[reg] & 0x1fff;
      v->banshee.blt.src_y = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_dstSize:
      v->banshee.blt.dst_w = v->banshee.blt.reg[reg] & 0x1fff;
      v->banshee.blt.dst_h = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_dstXY:
      v->banshee.blt.dst_x = v->banshee.blt.reg[reg] & 0x1fff;
      v->banshee.blt.dst_y = (v->banshee.blt.reg[reg] >> 16) & 0x1fff;
      break;
    case blt_command:
      v->banshee.blt.cmd = (value & 0x0f);
      v->banshee.blt.immed = (value >> 8) & 1;
      v->banshee.blt.x_dir = (value >> 14) & 1;
      v->banshee.blt.y_dir = (value >> 15) & 1;
      v->banshee.blt.transp = (value >> 16) & 1;
      v->banshee.blt.clip_sel = (value >> 23) & 1;
      v->banshee.blt.rop0 = (value >> 24);
      v->banshee.blt.dwcount = 0;
      switch (v->banshee.blt.cmd) {
        case 0: // NOP
          break;
        case 1:
          v->banshee.blt.busy = 1;
          banshee_blt_screen_to_screen();
          break;
        case 2:
          BX_INFO(("TODO: 2D Screen to screen stretch blt"));
          break;
        case 3:
          v->banshee.blt.busy = 1;
          if (!v->banshee.blt.immed) {
            sfmt = (v->banshee.blt.reg[blt_srcFormat] >> 16) & 0x0f;
            if (sfmt == 0) {
              pbytes = ((v->banshee.blt.dst_w + 7) >> 3) * v->banshee.blt.dst_h;
            } else if (sfmt == 1) {
              pbytes = v->banshee.blt.dst_w * v->banshee.blt.dst_h;
            } else if ((sfmt >= 3) && (sfmt <= 3))  {
              pbytes = v->banshee.blt.dst_w * v->banshee.blt.dst_h * (sfmt - 1);
            } else {
              pbytes = 0;
              BX_INFO(("Source format %d not handled yet", sfmt));
            }
            v->banshee.blt.dwcount = (pbytes + 3) >> 2;
            v->banshee.blt.lpidx = 0;
          } else {
            BX_ERROR(("Host to screen blt: immediate execution not supported"));
          }
          break;
        case 4:
          BX_INFO(("TODO: 2D Host to screen stretch blt"));
          break;
        case 5:
          v->banshee.blt.busy = 1;
          banshee_blt_rectangle_fill();
          break;
        case 6:
          BX_INFO(("TODO: 2D Line"));
          break;
        case 7:
          BX_INFO(("TODO: 2D Polyline"));
          break;
        case 8:
          BX_INFO(("TODO: 2D Polygon fill"));
          break;
        case 13:
          BX_INFO(("TODO: 2D Write Sgram Mode register"));
          return;
        case 14:
          BX_INFO(("TODO: 2D Write Sgram Mask register"));
          return;
        case 15:
          BX_INFO(("TODO: 2D Write Sgram Color register"));
          return;
        default:
          BX_ERROR(("Unsupported 2D mode"));
      }
      if (v->banshee.blt.immed) {
        banshee_blt_complete();
      }
      break;
    default:
      if ((reg >= 0x20) && (reg < 0x40)) {
        banshee_blt_launch_area_write(value);
      } else if ((reg >= 0x40) && (reg < 0x80)) {
        BX_DEBUG(("colorPattern write"));
      }
  }
}

void bx_voodoo_c::banshee_blt_launch_area_write(Bit32u value)
{
  if (v->banshee.blt.dwcount > 0) {
    BX_DEBUG(("launchArea write: value = 0x%08x", value));
    if (v->banshee.blt.lpidx > 4095) {
      BX_ERROR(("launchArea write: out of memory"));
      return;
    }
    v->banshee.blt.lpmem[v->banshee.blt.lpidx++] = (value & 0xff);
    v->banshee.blt.lpmem[v->banshee.blt.lpidx++] = ((value >> 8) & 0xff);
    v->banshee.blt.lpmem[v->banshee.blt.lpidx++] = ((value >> 16) & 0xff);
    v->banshee.blt.lpmem[v->banshee.blt.lpidx++] = ((value >> 24) & 0xff);
    if (--v->banshee.blt.dwcount == 0) {
      switch (v->banshee.blt.cmd) {
        case 3:
          banshee_blt_host_to_screen();
          break;
        default:
          BX_ERROR(("launchArea write: command %d not handled yet",
                    v->banshee.blt.cmd));
      }
      banshee_blt_complete();
    }
  }
}

void bx_voodoo_c::banshee_blt_complete()
{
  Bit32u start0 = v->banshee.io[io_vidDesktopStartAddr] & v->fbi.mask;
  Bit32u start1 = v->banshee.blt.reg[blt_dstBaseAddr] & v->fbi.mask;
  Bit16u pitch0 = v->banshee.io[io_vidDesktopOverlayStride] & 0x7fff;
  Bit32u pitch1 = v->banshee.blt.reg[blt_dstFormat] & 0x3fff;
  Bit32u cmd = v->banshee.blt.reg[blt_command];
  bx_bool xinc = (cmd >> 10) & 1;
  bx_bool yinc = (cmd >> 11) & 1;
  Bit32u offset0, offset1;
  Bit16u x, y;

  if (start1 <= start0) {
    offset0 = start1 + v->banshee.blt.dst_y * pitch1 + v->banshee.blt.dst_x * (v->banshee.bpp >> 3);
    offset1 = offset0 + v->banshee.blt.dst_h * pitch1 + v->banshee.blt.dst_w * (v->banshee.bpp >> 3);
    if (offset1 >= start0) {
      if (offset0 >= start0) {
        offset0 -= start0;
      } else {
        offset0 = 0;
      }
      x = (offset0 % pitch0) / (v->banshee.bpp >> 3);
      y = offset0 / pitch0;
      theVoodooVga->redraw_area(x, y, v->banshee.blt.dst_w, v->banshee.blt.dst_h);
    }
  } else {
    BX_ERROR(("redraw: Blt start > Desktop start not handled yet"));
  }
  if (xinc) {
    v->banshee.blt.dst_x += v->banshee.blt.dst_w;
    v->banshee.blt.reg[blt_dstXY] &= ~0xffff;
    v->banshee.blt.reg[blt_dstXY] |= v->banshee.blt.dst_x;
  }
  if (yinc) {
    v->banshee.blt.dst_y += v->banshee.blt.dst_h;
    v->banshee.blt.reg[blt_dstXY] &= 0xffff;
    v->banshee.blt.reg[blt_dstXY] |= (v->banshee.blt.dst_y << 16);
  }
}

void bx_voodoo_c::banshee_blt_rectangle_fill()
{
  Bit32u start = v->banshee.blt.reg[blt_dstBaseAddr] & v->fbi.mask;
  Bit8u *disp_ptr = &v->fbi.ram[start];
  Bit8u *vid_ptr, *vid_ptr2;
  Bit32u pitch = v->banshee.blt.reg[blt_dstFormat] & 0x3fff;
  Bit32u color;
  Bit16u x, y, x0, y0, w, h, cx0, cx1, cy0, cy1;
  Bit8u rop0, pxbytes = v->banshee.bpp >> 3;

  x0 = v->banshee.blt.dst_x;
  y0 = v->banshee.blt.dst_y;
  w = v->banshee.blt.dst_w;
  h = v->banshee.blt.dst_h;
  rop0 = v->banshee.blt.rop0;
  if (rop0 == 0xcc) {
    BX_DEBUG(("Rectangle fill (copy %d x %d)", w, h));
    cx0 = v->banshee.blt.clipx0[v->banshee.blt.clip_sel];
    cy0 = v->banshee.blt.clipy0[v->banshee.blt.clip_sel];
    cx1 = v->banshee.blt.clipx1[v->banshee.blt.clip_sel];
    cy1 = v->banshee.blt.clipy1[v->banshee.blt.clip_sel];
    color = v->banshee.blt.reg[blt_colorFore];
    vid_ptr = disp_ptr + y0 * pitch + x0 * pxbytes;
    for (y = y0; y < (y0 + h); y++) {
      vid_ptr2 = vid_ptr;
      for (x = x0; x < (x0 + w); x++) {
        if ((x >= cx0) && (x < cx1) && (y >= cy0) && (y < cy1)) {
          switch (v->banshee.bpp) {
            case 8:
              *(vid_ptr2++) = (color & 0xff);
              break;
            case 16:
              *(vid_ptr2++) = (color & 0xff);
              *(vid_ptr2++) = ((color >> 8) & 0xff);
              break;
            case 24:
              *(vid_ptr2++) = (color & 0xff);
              *(vid_ptr2++) = ((color >> 8) & 0xff);
              *(vid_ptr2++) = ((color >> 16) & 0xff);
              break;
            case 32:
              *(vid_ptr2++) = (color & 0xff);
              *(vid_ptr2++) = ((color >> 8) & 0xff);
              *(vid_ptr2++) = ((color >> 16) & 0xff);
              *(vid_ptr2++) = ((color >> 24) & 0xff);
              break;
          }
        } else {
          vid_ptr2 += pxbytes;
        }
      }
      vid_ptr += pitch;
    }
  } else {
    BX_INFO(("Rectangle fill: ROP 0x%02x not supported yet", rop0));
  }
  v->banshee.blt.busy = 0;
}

void bx_voodoo_c::banshee_blt_screen_to_screen()
{
  Bit32u sstart = v->banshee.io[blt_srcBaseAddr] & v->fbi.mask;
  Bit32u dstart = v->banshee.io[blt_dstBaseAddr] & v->fbi.mask;
  Bit32u pitch = v->banshee.io[blt_dstFormat] & 0x3fff;
  Bit8u *src_ptr = &v->fbi.ram[sstart];
  Bit8u *dst_ptr = &v->fbi.ram[dstart];
  Bit8u *src_ptr1, *src_ptr2, *dst_ptr1, *dst_ptr2;
  Bit16u x, y, x0, y0, w, h, cx0, cx1, cy0, cy1;
  Bit8u rop0, pxbytes = (v->banshee.bpp >> 3);

  x0 = v->banshee.blt.dst_x;
  y0 = v->banshee.blt.dst_y;
  w = v->banshee.blt.dst_w;
  h = v->banshee.blt.dst_h;
  rop0 = v->banshee.blt.rop0;
  if (rop0 == 0xcc) {
    BX_DEBUG(("Screen to screen blt (copy %d x %d)", w, h));
    cx0 = v->banshee.blt.clipx0[v->banshee.blt.clip_sel];
    cy0 = v->banshee.blt.clipy0[v->banshee.blt.clip_sel];
    cx1 = v->banshee.blt.clipx1[v->banshee.blt.clip_sel];
    cy1 = v->banshee.blt.clipy1[v->banshee.blt.clip_sel];
    src_ptr1 = src_ptr + (y0 * pitch + x0);
    dst_ptr1 = dst_ptr + (y0 * pitch + x0);
    if (v->banshee.blt.y_dir) {
      if (v->banshee.blt.x_dir) {
        for (y = y0; y > (y0 - h); y--) {
          src_ptr2 = src_ptr1;
          dst_ptr2 = dst_ptr1;
          for (x = x0; x > (x0 - w); x--) {
            if ((x >= cx0) && (x < cx1) && (y >= cy0) && (y < cy1)) {
               memcpy(dst_ptr2, src_ptr2, pxbytes);
            }
            src_ptr2 -= pxbytes;
            dst_ptr2 -= pxbytes;
          }
          src_ptr1 -= pitch;
          dst_ptr1 -= pitch;
        }
      } else {
        for (y = y0; y > (y0 - h); y--) {
          src_ptr2 = src_ptr1;
          dst_ptr2 = dst_ptr1;
          for (x = x0; x < (x0 + w); x++) {
            if ((x >= cx0) && (x < cx1) && (y >= cy0) && (y < cy1)) {
              memcpy(dst_ptr2, src_ptr2, pxbytes);
            }
            src_ptr2 += pxbytes;
            dst_ptr2 += pxbytes;
          }
          src_ptr1 -= pitch;
          dst_ptr1 -= pitch;
        }
      }
    } else {
      if (v->banshee.blt.x_dir) {
        for (y = y0; y < (y0 + h); y++) {
          src_ptr2 = src_ptr1;
          dst_ptr2 = dst_ptr1;
          for (x = x0; x > (x0 - w); x--) {
            if ((x >= cx0) && (x < cx1) && (y >= cy0) && (y < cy1)) {
              memcpy(dst_ptr2, src_ptr2, pxbytes);
            }
            src_ptr2 -= pxbytes;
            dst_ptr2 -= pxbytes;
          }
          src_ptr1 += pitch;
          dst_ptr1 += pitch;
        }
      } else {
        for (y = y0; y < (y0 + h); y++) {
          src_ptr2 = src_ptr1;
          dst_ptr2 = dst_ptr1;
          for (x = x0; x < (x0 + w); x++) {
            if ((x >= cx0) && (x < cx1) && (y >= cy0) && (y < cy1)) {
              memcpy(dst_ptr2, src_ptr2, pxbytes);
            }
            src_ptr2 += pxbytes;
            dst_ptr2 += pxbytes;
          }
          src_ptr1 += pitch;
          dst_ptr1 += pitch;
        }
      }
    }
  } else {
    BX_INFO(("Screen to screen blt: ROP 0x%02x not supported yet", rop0));
  }
  v->banshee.blt.busy = 0;
}

void bx_voodoo_c::banshee_blt_host_to_screen()
{
  Bit32u start = v->banshee.blt.reg[blt_dstBaseAddr] & v->fbi.mask;
  Bit8u *disp_ptr = &v->fbi.ram[start];
  Bit8u *src_ptr = &v->banshee.blt.lpmem[0];
  Bit8u *vid_ptr, *vid_ptr2;
  Bit32u pitch = v->banshee.blt.reg[blt_dstFormat] & 0x3fff;
  Bit8u pxbytes = v->banshee.bpp >> 3;
  Bit32u color, fgcolor, bgcolor, scolor;
  Bit16u i, x, y, x0, y0, w, h, cx0, cx1, cy0, cy1;
  Bit8u mask, rop0;

  x0 = v->banshee.blt.dst_x;
  y0 = v->banshee.blt.dst_y;
  w = v->banshee.blt.dst_w;
  h = v->banshee.blt.dst_h;
  rop0 = v->banshee.blt.rop0;
  if (rop0 == 0xcc) {
    BX_DEBUG(("Host to screen blt (copy %d x %d)", w, h));
    cx0 = v->banshee.blt.clipx0[v->banshee.blt.clip_sel];
    cy0 = v->banshee.blt.clipy0[v->banshee.blt.clip_sel];
    cx1 = v->banshee.blt.clipx1[v->banshee.blt.clip_sel];
    cy1 = v->banshee.blt.clipy1[v->banshee.blt.clip_sel];
    fgcolor = v->banshee.blt.reg[blt_colorFore];
    bgcolor = v->banshee.blt.reg[blt_colorBack];
    vid_ptr = disp_ptr + y0 * pitch + x0 * pxbytes;
    for (y = y0; y < (y0 + h); y++) {
      vid_ptr2 = vid_ptr;
      mask = 0x80;
      for (x = x0; x < (x0 + w); x++) {
        if ((x >= cx0) && (x < cx1) && (y >= cy0) && (y < cy1)) {
          scolor = 0;
          for (i = 0; i < pxbytes; i++) {
            scolor |= (vid_ptr2[i] << (i * 8));
          }
          if (*src_ptr & mask) {
            color = fgcolor;
          } else if (!v->banshee.blt.transp) {
            color = bgcolor;
          } else {
            color = scolor;
          }
          switch (v->banshee.bpp) {
            case 8:
              *(vid_ptr2++) = (color & 0xff);
              break;
            case 16:
              *(vid_ptr2++) = (color & 0xff);
              *(vid_ptr2++) = ((color >> 8) & 0xff);
              break;
            case 24:
              *(vid_ptr2++) = (color & 0xff);
              *(vid_ptr2++) = ((color >> 8) & 0xff);
              *(vid_ptr2++) = ((color >> 16) & 0xff);
              break;
            case 32:
              *(vid_ptr2++) = (color & 0xff);
              *(vid_ptr2++) = ((color >> 8) & 0xff);
              *(vid_ptr2++) = ((color >> 16) & 0xff);
              *(vid_ptr2++) = ((color >> 24) & 0xff);
              break;
          }
        } else {
          vid_ptr2 += pxbytes;
        }
        mask >>= 1;
        if (mask == 0) {
          src_ptr++;
          mask = 0x80;
        }
      }
      if (mask != 0x80) {
        src_ptr++;
      }
      vid_ptr += pitch;
    }
  } else {
    BX_INFO(("Host to screen blt: ROP 0x%02x not supported yet", rop0));
  }
  v->banshee.blt.busy = 0;
}

#undef LOG_THIS
#define LOG_THIS theVoodooVga->

bx_voodoo_vga_c::bx_voodoo_vga_c() : bx_vgacore_c()
{
  put("VVGA");
}

bx_voodoo_vga_c::~bx_voodoo_vga_c()
{
  s.memory = NULL;
}

bx_bool bx_voodoo_vga_c::init_vga_extension(void)
{
  theVoodooDevice->init();
  if (theVoodooDevice->get_model() == VOODOO_BANSHEE) {
    BX_VVGA_THIS s.memory = v->fbi.ram;
    BX_VVGA_THIS s.memsize = v->fbi.mask + 1;
    init_iohandlers(banshee_vga_read_handler, banshee_vga_write_handler);
    DEV_register_iowrite_handler(this, banshee_vga_write_handler, 0x0102, "banshee", 1);
    DEV_register_iowrite_handler(this, banshee_vga_write_handler, 0x46e8, "banshee", 1);
    BX_VVGA_THIS s.max_xres = 1600;
    BX_VVGA_THIS s.max_yres = 1280;
    v->banshee.bpp = 8;
    BX_VVGA_THIS s.vclk[0] = 25175000;
    BX_VVGA_THIS s.vclk[1] = 28322000;
    BX_VVGA_THIS s.vclk[2] = 50000000;
    BX_VVGA_THIS s.vclk[3] = 25175000;
    BX_VVGA_THIS pci_enabled = 1;
    return 1;
  } else {
    init_iohandlers(read_handler, write_handler);
    return 0;
  }
}

void bx_voodoo_vga_c::reset(unsigned type)
{
  theVoodooDevice->reset(type);
}

void bx_voodoo_vga_c::register_state(void)
{
  bx_list_c *list = new bx_list_c(SIM->get_bochs_root(), "voodoo_vga", "Voodoo VGA State");
  bx_vgacore_c::register_state(list);
  theVoodooDevice->register_state();
}

void bx_voodoo_vga_c::after_restore_state(void)
{
  bx_vgacore_c::after_restore_state();
  theVoodooDevice->after_restore_state();
}

void bx_voodoo_vga_c::redraw_area(unsigned x0, unsigned y0, unsigned width,
                                  unsigned height)
{
  unsigned xt0, xt1, xti, yt0, yt1, yti;

  if (v->banshee.io[io_vidProcCfg] & 0x01) {
    xt0 = x0 / X_TILESIZE;
    yt0 = y0 / Y_TILESIZE;
    xt1 = (x0 + width  - 1) / X_TILESIZE;
    yt1 = (y0 + height - 1) / Y_TILESIZE;
    for (yti=yt0; yti<=yt1; yti++) {
      for (xti=xt0; xti<=xt1; xti++) {
        SET_TILE_UPDATED(BX_VVGA_THIS, xti, yti, 1);
      }
    }
  } else {
    bx_vgacore_c::redraw_area(x0, y0, width, height);
  }
}

void bx_voodoo_vga_c::banshee_update_mode(void)
{
  Bit8u format = (v->banshee.io[io_vidProcCfg] >> 18) & 0x07;

  if (format < 4) {
    v->banshee.bpp = (format + 1) << 3;
  } else {
    BX_ERROR(("Ignoring reserved pixel format"));
    return;
  }
  vbe.half_mode = (v->banshee.io[io_vidProcCfg] >> 4) & 1;
  BX_INFO(("switched to %d x %d x %d", v->fbi.width, v->fbi.height, v->banshee.bpp));
  bx_gui->dimension_update(v->fbi.width, v->fbi.height, 0, 0, v->banshee.bpp);
  // compatibilty settings for VGA core
  BX_VVGA_THIS s.last_xres = v->fbi.width;
  BX_VVGA_THIS s.last_yres = v->fbi.height;
  BX_VVGA_THIS s.last_bpp = v->banshee.bpp;
  BX_VVGA_THIS s.last_fh = 0;
}

void bx_voodoo_vga_c::banshee_set_dac_mode(bx_bool mode)
{
  unsigned i;

  if (mode != vbe.dac_8bit) {
    if (mode) {
      for (i=0; i<256; i++) {
        s.pel.data[i].red <<= 2;
        s.pel.data[i].green <<= 2;
        s.pel.data[i].blue <<= 2;
      }
      BX_INFO(("DAC in 8 bit mode"));
    } else {
      for (i=0; i<256; i++) {
        s.pel.data[i].red >>= 2;
        s.pel.data[i].green >>= 2;
        s.pel.data[i].blue >>= 2;
      }
      BX_INFO(("DAC in standard mode"));
    }
    vbe.dac_8bit = mode;
    s.dac_shift = mode ? 0 : 2;
  }
}

void bx_voodoo_vga_c::banshee_set_vclk3(Bit32u value)
{
  BX_VVGA_THIS s.vclk[3] = value;
  if (BX_VVGA_THIS s.misc_output.clock_select == 3) {
    calculate_retrace_timing();
  }
}

void bx_voodoo_vga_c::get_crtc_params(Bit32u *htotal, Bit32u *vtotal)
{
  *htotal = BX_VVGA_THIS s.CRTC.reg[0] + ((v->banshee.crtc[0x1a] & 0x01) << 8) + 5;
  *vtotal = BX_VVGA_THIS s.CRTC.reg[6] + ((BX_VVGA_THIS s.CRTC.reg[7] & 0x01) << 8) +
            ((BX_VVGA_THIS s.CRTC.reg[7] & 0x20) << 4) +
            ((v->banshee.crtc[0x1b] & 0x01) << 10) + 2;
}

void bx_voodoo_vga_c::banshee_draw_hwcursor(unsigned xc, unsigned yc, bx_svga_tileinfo_t *info)
{
  unsigned cx, cy, cw, ch, px, py, w, h, x, y;
  Bit8u *cpat0, *cpat1, *tile_ptr, *tile_ptr2, *vid_ptr;
  Bit8u ccode, pbits, pval0, pval1;
  Bit32u colour = 0;
  int i;

  if ((xc <= v->banshee.hwcursor.x) &&
      ((int)(xc + X_TILESIZE) > (v->banshee.hwcursor.x - 63)) &&
      (yc <= v->banshee.hwcursor.y) &&
      ((int)(yc + Y_TILESIZE) > (v->banshee.hwcursor.y - 63))) {

    Bit32u start = v->banshee.io[io_vidDesktopStartAddr];
    Bit8u *disp_ptr = &v->fbi.ram[start & v->fbi.mask];
    Bit16u pitch = v->banshee.io[io_vidDesktopOverlayStride] & 0x7fff;
    tile_ptr = bx_gui->graphics_tile_get(xc, yc, &w, &h);

    if ((v->banshee.hwcursor.x - 63) < (int)xc) {
      cx = xc;
      if ((v->banshee.hwcursor.x - xc + 1) > w) {
        cw = w;
      } else {
        cw = v->banshee.hwcursor.x - xc + 1;
      }
      px = 63 - (v->banshee.hwcursor.x - xc);
    } else {
      cx = v->banshee.hwcursor.x - 63;
      cw = w - (v->banshee.hwcursor.x - 63 - xc);
      px = 0;
    }
    if ((v->banshee.hwcursor.y - 63) < (int)yc) {
      cy = yc;
      if ((v->banshee.hwcursor.y - yc + 1) > h) {
        ch = h;
      } else {
        ch = v->banshee.hwcursor.y - yc + 1;
      }
      py = 63 - (v->banshee.hwcursor.y - yc);
    } else {
      cy = v->banshee.hwcursor.y - 63;
      ch = h - (v->banshee.hwcursor.y - 63 - yc);
      py = 0;
    }
    tile_ptr += ((cy - yc) * info->pitch);
    tile_ptr += ((cx - xc) * (info->bpp >> 3));
    cpat0 = &v->fbi.ram[v->banshee.hwcursor.addr] + (py * 16);
    for (y = cy; y < (cy + ch); y++) {
      cpat1 = cpat0 + (px >> 3);
      pbits = 8 - (px & 7);
      tile_ptr2 = tile_ptr;
      for (x = cx; x < (cx + cw); x++) {
        pval0 = (*cpat1 >> (pbits - 1)) & 1;
        pval1 = (*(cpat1 + 8) >> (pbits - 1)) & 1;
        ccode = pval0 + (pval1 << 1) + (v->banshee.hwcursor.mode << 2);
        if ((ccode == 0) || (ccode == 5)) {
          colour = v->banshee.hwcursor.color[0];
        } else if ((ccode == 2) || (ccode == 7)) {
          colour = v->banshee.hwcursor.color[1];
        } else {
          vid_ptr = disp_ptr + y * pitch + x * (v->banshee.bpp >> 3);
          switch (v->banshee.bpp) {
            case 8:
              colour = v->fbi.clut[*vid_ptr];
              break;
            case 16:
              colour = *vid_ptr;
              colour |= (*(vid_ptr + 1)) << 8;
              colour = (((colour & 0xf800) << 8) | ((colour & 0x07e0) << 5) |
                        ((colour & 0x001f) << 3));
              break;
            case 24:
            case 32:
              colour = *vid_ptr;
              colour |= (*(vid_ptr + 1)) << 8;
              colour |= (*(vid_ptr + 2)) << 16;
              break;
          }
          if (ccode == 3) colour ^= 0xffffff;
        }
        colour = MAKE_COLOUR(
          colour, 24, info->red_shift, info->red_mask,
          colour, 16, info->green_shift, info->green_mask,
          colour, 8, info->blue_shift, info->blue_mask);
        if (info->is_little_endian) {
          for (i=0; i<info->bpp; i+=8) {
            *(tile_ptr2++) = (Bit8u)(colour >> i);
          }
        } else {
          for (i=info->bpp-8; i>-8; i-=8) {
            *(tile_ptr2++) = (Bit8u)(colour >> i);
          }
        }
        if (--pbits == 0) {
          cpat1++;
          pbits = 8;
        }
      }
      cpat0 += 16;
      tile_ptr += info->pitch;
    }
  }
}

void bx_voodoo_vga_c::update(void)
{
  Bit32u start = v->banshee.io[io_vidDesktopStartAddr];
  unsigned iHeight, iWidth;
  unsigned pitch, xc, yc, xti, yti;
  unsigned r, c, w, h;
  int i;
  Bit32u red, green, blue, colour;
  Bit8u *vid_ptr, *vid_ptr2;
  Bit8u *tile_ptr, *tile_ptr2;
  bx_svga_tileinfo_t info;

  if (v->banshee.io[io_vidProcCfg] & 0x01) {
    iWidth = v->fbi.width;
    iHeight = v->fbi.height;
    if (v->fbi.clut_dirty || v->fbi.video_changed) {
      redraw_area(0, 0, iWidth, iHeight);
      v->fbi.clut_dirty = 0;
      v->fbi.video_changed = 0;
    }
    pitch = v->banshee.io[io_vidDesktopOverlayStride] & 0x7fff;
    Bit8u *disp_ptr = &v->fbi.ram[start & v->fbi.mask];
    if (bx_gui->graphics_tile_info_common(&info)) {
      if (info.snapshot_mode) {
        vid_ptr = disp_ptr;
        tile_ptr = bx_gui->get_snapshot_buffer();
        if (tile_ptr != NULL) {
          for (yc = 0; yc < iHeight; yc++) {
            memcpy(tile_ptr, vid_ptr, info.pitch);
            vid_ptr += pitch;
            tile_ptr += info.pitch;
          }
        }
      } else if (info.is_indexed) {
        BX_ERROR(("current guest pixel format is unsupported on indexed colour host displays"));
      } else {
        switch (v->banshee.bpp) {
          case 8:
            for (yc=0, yti = 0; yc<iHeight; yc+=Y_TILESIZE, yti++) {
              for (xc=0, xti = 0; xc<iWidth; xc+=X_TILESIZE, xti++) {
                if (GET_TILE_UPDATED(xti, yti)) {
                  if (BX_VVGA_THIS vbe.half_mode) {
                    vid_ptr = disp_ptr + ((yc >> 1) * pitch + xc);
                  } else {
                    vid_ptr = disp_ptr + (yc * pitch + xc);
                  }
                  tile_ptr = bx_gui->graphics_tile_get(xc, yc, &w, &h);
                  for (r=0; r<h; r++) {
                    vid_ptr2  = vid_ptr;
                    tile_ptr2 = tile_ptr;
                    for (c=0; c<w; c++) {
                      colour = v->fbi.clut[*(vid_ptr2++)];
                      colour = MAKE_COLOUR(
                        colour & 0xff0000, 24, info.red_shift, info.red_mask,
                        colour & 0x00ff00, 16, info.green_shift, info.green_mask,
                        colour & 0x0000ff, 8, info.blue_shift, info.blue_mask);
                      if (info.is_little_endian) {
                        for (i=0; i<info.bpp; i+=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      } else {
                        for (i=info.bpp-8; i>-8; i-=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      }
                    }
                    if (!BX_VVGA_THIS vbe.half_mode || (r & 1)) {
                      vid_ptr += pitch;
                    }
                    tile_ptr += info.pitch;
                  }
                  if (v->banshee.hwcursor.enabled) {
                    banshee_draw_hwcursor(xc, yc, &info);
                  }
                  SET_TILE_UPDATED(BX_VVGA_THIS, xti, yti, 0);
                  bx_gui->graphics_tile_update_in_place(xc, yc, w, h);
                }
              }
            }
            break;
          case 16:
            for (yc=0, yti = 0; yc<iHeight; yc+=Y_TILESIZE, yti++) {
              for (xc=0, xti = 0; xc<iWidth; xc+=X_TILESIZE, xti++) {
                if (GET_TILE_UPDATED (xti, yti)) {
                  if (BX_VVGA_THIS vbe.half_mode) {
                    vid_ptr = disp_ptr + ((yc >> 1) * pitch + (xc << 1));
                  } else {
                    vid_ptr = disp_ptr + (yc * pitch + (xc << 1));
                  }
                  tile_ptr = bx_gui->graphics_tile_get(xc, yc, &w, &h);
                  for (r=0; r<h; r++) {
                    vid_ptr2  = vid_ptr;
                    tile_ptr2 = tile_ptr;
                    for (c=0; c<w; c++) {
                      colour = *(vid_ptr2++);
                      colour |= *(vid_ptr2++) << 8;
                      colour = MAKE_COLOUR(
                        colour & 0x001f, 5, info.blue_shift, info.blue_mask,
                        colour & 0x07e0, 11, info.green_shift, info.green_mask,
                        colour & 0xf800, 16, info.red_shift, info.red_mask);
                      if (info.is_little_endian) {
                        for (i=0; i<info.bpp; i+=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      } else {
                        for (i=info.bpp-8; i>-8; i-=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      }
                    }
                    if (!BX_VVGA_THIS vbe.half_mode || (r & 1)) {
                      vid_ptr += pitch;
                    }
                    tile_ptr += info.pitch;
                  }
                  if (v->banshee.hwcursor.enabled) {
                    banshee_draw_hwcursor(xc, yc, &info);
                  }
                  SET_TILE_UPDATED(BX_VVGA_THIS, xti, yti, 0);
                  bx_gui->graphics_tile_update_in_place(xc, yc, w, h);
                }
              }
            }
            break;
          case 24:
            for (yc=0, yti = 0; yc<iHeight; yc+=Y_TILESIZE, yti++) {
              for (xc=0, xti = 0; xc<iWidth; xc+=X_TILESIZE, xti++) {
                if (GET_TILE_UPDATED (xti, yti)) {
                  if (BX_VVGA_THIS vbe.half_mode) {
                    vid_ptr = disp_ptr + ((yc >> 1) * pitch + 3*xc);
                  } else {
                    vid_ptr = disp_ptr + (yc * pitch + 3*xc);
                  }
                  tile_ptr = bx_gui->graphics_tile_get(xc, yc, &w, &h);
                  for (r=0; r<h; r++) {
                    vid_ptr2  = vid_ptr;
                    tile_ptr2 = tile_ptr;
                    for (c=0; c<w; c++) {
                      blue = *(vid_ptr2++);
                      green = *(vid_ptr2++);
                      red = *(vid_ptr2++);
                      colour = MAKE_COLOUR(
                        red, 8, info.red_shift, info.red_mask,
                        green, 8, info.green_shift, info.green_mask,
                        blue, 8, info.blue_shift, info.blue_mask);
                      if (info.is_little_endian) {
                        for (i=0; i<info.bpp; i+=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      } else {
                        for (i=info.bpp-8; i>-8; i-=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      }
                    }
                    if (!BX_VVGA_THIS vbe.half_mode || (r & 1)) {
                      vid_ptr += pitch;
                    }
                    tile_ptr += info.pitch;
                  }
                  if (v->banshee.hwcursor.enabled) {
                    banshee_draw_hwcursor(xc, yc, &info);
                  }
                  SET_TILE_UPDATED(BX_VVGA_THIS, xti, yti, 0);
                  bx_gui->graphics_tile_update_in_place(xc, yc, w, h);
                }
              }
            }
            break;
          case 32:
            for (yc=0, yti = 0; yc<iHeight; yc+=Y_TILESIZE, yti++) {
              for (xc=0, xti = 0; xc<iWidth; xc+=X_TILESIZE, xti++) {
                if (GET_TILE_UPDATED (xti, yti)) {
                  if (BX_VVGA_THIS vbe.half_mode) {
                    vid_ptr = disp_ptr + ((yc >> 1) * pitch + (xc << 2));
                  } else {
                    vid_ptr = disp_ptr + (yc * pitch + (xc << 2));
                  }
                  tile_ptr = bx_gui->graphics_tile_get(xc, yc, &w, &h);
                  for (r=0; r<h; r++) {
                    vid_ptr2  = vid_ptr;
                    tile_ptr2 = tile_ptr;
                    for (c=0; c<w; c++) {
                      blue = *(vid_ptr2++);
                      green = *(vid_ptr2++);
                      red = *(vid_ptr2++);
                      vid_ptr2++;
                      colour = MAKE_COLOUR(
                        red, 8, info.red_shift, info.red_mask,
                        green, 8, info.green_shift, info.green_mask,
                        blue, 8, info.blue_shift, info.blue_mask);
                      if (info.is_little_endian) {
                        for (i=0; i<info.bpp; i+=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      } else {
                        for (i=info.bpp-8; i>-8; i-=8) {
                          *(tile_ptr2++) = (Bit8u)(colour >> i);
                        }
                      }
                    }
                    if (!BX_VVGA_THIS vbe.half_mode || (r & 1)) {
                      vid_ptr += pitch;
                    }
                    tile_ptr += info.pitch;
                  }
                  if (v->banshee.hwcursor.enabled) {
                    banshee_draw_hwcursor(xc, yc, &info);
                  }
                  SET_TILE_UPDATED(BX_VVGA_THIS, xti, yti, 0);
                  bx_gui->graphics_tile_update_in_place(xc, yc, w, h);
                }
              }
            }
            break;
          default:
            BX_ERROR(("Ignoring reserved pixel format"));
        }
      }
    } else {
      BX_PANIC(("cannot get svga tile info"));
    }
  } else if (!((v->banshee.io[io_vgaInit0] >> 12) & 1)) {
    BX_VVGA_THIS bx_vgacore_c::update();
  }
}

Bit32u bx_voodoo_vga_c::banshee_vga_read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
  UNUSED(this_ptr);
  Bit32u value;

  if ((io_len == 2) && ((address & 1) == 0)) {
    value = banshee_vga_read_handler(theVoodooVga,address,1);
    value |= (banshee_vga_read_handler(theVoodooVga,address+1,1) << 8);
    return value;
  }

  if (BX_VVGA_THIS s.misc_output.color_emulation && (address == 0x03b5))
    return 0xff;
  if (!BX_VVGA_THIS s.misc_output.color_emulation && (address == 0x03d5))
    return 0xff;

  switch (address) {
    case 0x03b5:
    case 0x03d5:
      if (BX_VVGA_THIS s.CRTC.address > 0x18) {
        if (BX_VVGA_THIS s.CRTC.address <= 0x26) {
          if ((v->banshee.io[io_vgaInit0] & 0x440) == 0x040) {
            value = v->banshee.crtc[BX_VVGA_THIS s.CRTC.address];
            BX_DEBUG(("read from banshee CRTC address 0x%02x value 0x%02x",
                      BX_VVGA_THIS s.CRTC.address, value));
          } else {
            value = 0xff;
          }
        } else {
          value = 0xff;
        }
        break;
      }
    default:
      value = bx_vgacore_c::read_handler(BX_VVGA_THIS_PTR, address, io_len);
  }
  return value;

}

void bx_voodoo_vga_c::banshee_vga_write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
  UNUSED(this_ptr);
  Bit8u value8;

  if ((io_len == 2) && ((address & 1) == 0)) {
    banshee_vga_write_handler(theVoodooVga,address,value & 0xff,1);
    banshee_vga_write_handler(theVoodooVga,address+1,value >> 8,1);
    return;
  }

  if (BX_VVGA_THIS s.misc_output.color_emulation && (address == 0x03b5))
    return;
  if (!BX_VVGA_THIS s.misc_output.color_emulation && (address == 0x03d5))
    return;

  switch (address) {
    case 0x0102:
    case 0x46e8:
      // TODO
      return;

    case 0x03c9:
      value8 = (Bit8u)value;
      if (!BX_VVGA_THIS vbe.dac_8bit) value8 <<= 2;
      switch (BX_VVGA_THIS s.pel.write_data_cycle) {
        case 0:
          v->fbi.clut[BX_VVGA_THIS s.pel.write_data_register] &= 0x00ffff;
          v->fbi.clut[BX_VVGA_THIS s.pel.write_data_register] |= (value8 << 16);
          break;
        case 1:
          v->fbi.clut[BX_VVGA_THIS s.pel.write_data_register] &= 0xff00ff;
          v->fbi.clut[BX_VVGA_THIS s.pel.write_data_register] |= (value8 << 8);
          break;
        case 2:
          v->fbi.clut[BX_VVGA_THIS s.pel.write_data_register] &= 0xffff00;
          v->fbi.clut[BX_VVGA_THIS s.pel.write_data_register] |= value8;
          break;
      }
      break;

    case 0x03b5:
    case 0x03d5:
      if (BX_VVGA_THIS s.CRTC.address > 0x18) {
        if (BX_VVGA_THIS s.CRTC.address <= 0x26) {
          if ((v->banshee.io[io_vgaInit0] & 0x440) == 0x040) {
            BX_DEBUG(("write to banshee CRTC address 0x%02x value 0x%02x",
                      BX_VVGA_THIS s.CRTC.address, value));
            v->banshee.crtc[BX_VVGA_THIS s.CRTC.address] = (Bit8u)value;
          }
        }
        return;
      }
      break;
  }
  bx_vgacore_c::write_handler(theVoodooVga, address, value, io_len);
}

Bit8u bx_voodoo_vga_c::mem_read(bx_phy_address addr)
{
  Bit32u offset = ((v->banshee.io[io_vgaInit1] & 0xffc00) << 5) + (addr & 0x1ffff);
  bx_bool chain4 = ((v->banshee.io[io_vgaInit1] >> 20) & 1);

  if (chain4) {
    return v->fbi.ram[offset & v->fbi.mask];
  } else {
    return bx_vgacore_c::mem_read(addr);
  }
}

void bx_voodoo_vga_c::mem_write(bx_phy_address addr, Bit8u value)
{
  bx_bool chain4 = ((v->banshee.io[io_vgaInit1] >> 20) & 1);
  Bit32u offset, start, end, pitch;
  unsigned xti, yti;

  if (chain4) {
    offset = (((v->banshee.io[io_vgaInit1] & 0x3ff) << 15) + (addr & 0x1ffff)) & v->fbi.mask;
    v->fbi.ram[offset] = value;
    start = v->banshee.io[io_vidDesktopStartAddr] & v->fbi.mask;
    pitch = v->banshee.io[io_vidDesktopOverlayStride] & 0x7fff;
    end = start + pitch * v->fbi.height;
    if ((offset >= start) && (offset < end)) {
      offset -= start;
      if (BX_VVGA_THIS vbe.half_mode) {
        yti = (offset / pitch) / (Y_TILESIZE / 2);
      } else {
        yti = (offset / pitch) / Y_TILESIZE;
      }
      xti = ((offset % pitch) / (v->banshee.bpp >> 3)) / X_TILESIZE;
      SET_TILE_UPDATED(BX_VVGA_THIS, xti, yti, 1);
    }
  } else {
    bx_vgacore_c::mem_write(addr, value);
  }
}

#endif // BX_SUPPORT_PCI && BX_SUPPORT_VOODOO
