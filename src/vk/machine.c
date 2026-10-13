/* 
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 * 
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vk/machine.h"
#include "vk/state.h"
#include "vk/buffer.h"
#include "vk/device.h"
#include "vk/cpu.h"

void
vk_machine_destroy (vk_machine_t **mach_)
{
	if (mach_) {
		vk_machine_t *mach = *mach_;
		unsigned i;

		vk_renderer_destroy (&mach->renderer);
		mach->destroy (mach_);

		VK_VECTOR_FOREACH (mach->buffers, i) {
			vk_buffer_t *buf = *(vk_buffer_t **) &mach->buffers->data[i];
			vk_buffer_destroy (&buf);
		}
		vk_vector_destroy (&mach->buffers);

		VK_VECTOR_FOREACH (mach->devices, i) {
			vk_device_t *dev = *(vk_device_t **) &mach->devices->data[i];
			vk_device_destroy (&dev);
		}
		vk_vector_destroy (&mach->devices);

		vk_vector_destroy (&mach->cpus);

		free (mach);
		*mach_ = NULL;
	}
}

static void
register_component (vk_vector_t **where_, void *what, unsigned num_default)
{
	void **entry;

	VK_ASSERT (where_);
	VK_ASSERT (what);
	VK_ASSERT (num_default);

	if (!*where_) {
		*where_ = vk_vector_new (num_default, sizeof (void *));
		VK_ASSERT (*where_);
	}

	entry = (void **) vk_vector_append_entry (*where_);
	VK_ASSERT (entry);

	*entry = what;
}

void
vk_machine_register_buffer (vk_machine_t *mach, void *ptr)
{
	register_component (&mach->buffers, ptr, 16);
	VK_LOG ("machine: registered buffer %p, size=%08X",
	        ptr, vk_buffer_get_size ((vk_buffer_t *) ptr));
}

void
vk_machine_register_device (vk_machine_t *mach, void *ptr)
{
	register_component (&mach->devices, ptr, 16);
	VK_LOG ("machine: registered device %p", ptr);
}

void
vk_machine_register_cpu (vk_machine_t *mach, void *ptr)
{
	register_component (&mach->cpus, ptr, 4);
	VK_LOG ("machine: registered cpu %p", ptr);
}

void
vk_machine_reset (vk_machine_t *mach, vk_reset_type_t type)
{
	unsigned offs;

	VK_LOG ("resetting machine %p", mach);

	/* The frame being drawn reads the memories. */
	vk_renderer_finish (mach->renderer);

	mach->resetting = true;

	VK_VECTOR_FOREACH (mach->buffers, offs) {
		vk_buffer_t *buf = *(vk_buffer_t **) &mach->buffers->data[offs];
		VK_LOG ("resetting buf %p", (void *) buf);
		vk_buffer_clear (buf);
	}

	VK_VECTOR_FOREACH (mach->devices, offs) {
		vk_device_t *dev = *((vk_device_t **) &mach->devices->data[offs]);
		VK_LOG ("resetting dev %p", (void *) dev);
		vk_device_reset (dev, type);
	}

	mach->resetting = false;
	mach->reset (mach, type);

	vk_renderer_reset (mach->renderer);

	if (type == VK_RESET_TYPE_HARD)
		mach->frame = 0;
}

int
vk_machine_run_frame (vk_machine_t *mach)
{
	int ret = mach->run_frame (mach);
	mach->frame++;
	return ret;
}

static int
load_save_state (vk_machine_t *mach, const char *path, uint32_t mode)
{
	vk_state_t *state;
	char name[32];
	char *op;
	unsigned i;
	int ret = 0;

	VK_ASSERT (mach);

	op = (mode == VK_STATE_LOAD) ? "load" : "save";

	state = vk_state_new (path, mode);
	if (!state) {
		VK_ERROR ("%s state: cannot create state object", op);
		ret = -1;
		goto done;
	}

	if (mode == VK_STATE_LOAD)
		vk_machine_reset (mach, VK_RESET_TYPE_HARD);
	else if (mach->settle)
		mach->settle (mach);

	/* The game and the frame number */
	memset (name, 0, sizeof (name));
	if (mode == VK_STATE_LOAD) {
		ret = vk_state_get (state, name, sizeof (name));
		if (!ret && strncmp (name, mach->game->name, sizeof (name))) {
			VK_ERROR ("load state: made with the game '%.32s'", name);
			ret = -1;
		}
		if (!ret)
			ret = vk_state_get (state, &mach->frame, sizeof (mach->frame));
	} else {
		strncpy (name, mach->game->name, sizeof (name) - 1);
		ret = vk_state_put (state, name, sizeof (name));
		if (!ret)
			ret = vk_state_put (state, &mach->frame, sizeof (mach->frame));
	}
	if (ret) {
		VK_ERROR ("%s state: cannot %s header", op, op);
		goto done;
	}

	VK_VECTOR_FOREACH (mach->buffers, i) {
		vk_buffer_t *buf = *(vk_buffer_t **) &mach->buffers->data[i];
		ret = (mode == VK_STATE_LOAD) ?
		      vk_buffer_load_state (buf, state) :
		      vk_buffer_save_state (buf, state);
		if (ret) {
			VK_ERROR ("%s state: cannot %s buffer", op, op);
			goto done;
		}
	}

	VK_VECTOR_FOREACH (mach->devices, i) {
		vk_device_t *dev = *(vk_device_t **) &mach->devices->data[i];
		ret = (mode == VK_STATE_LOAD) ?
		      vk_device_load_state (dev, state) :
		      vk_device_save_state (dev, state);
		if (ret) {
			VK_ERROR ("%s state: cannot %s device", op, op);
			goto done;
		}
	}

	ret = (mode == VK_STATE_LOAD) ? mach->load_state (mach, state) :
	                                mach->save_state (mach, state);
	if (ret) {
		VK_ERROR ("%s state; cannot %s machine", op, op);
		goto done;
	}

done:
	if (state)
		vk_state_destroy (&state, ret);
	if (ret && mode == VK_STATE_LOAD) {
		VK_ERROR ("load state: resetting machine");
		vk_machine_reset (mach, VK_RESET_TYPE_HARD);
	}
	return ret;
}

int
vk_machine_load_state (vk_machine_t *mach, const char *path)
{
	return load_save_state (mach, path, VK_STATE_LOAD);
}

int
vk_machine_save_state (vk_machine_t *mach, const char *path)
{
	return load_save_state (mach, path, VK_STATE_SAVE);
}

const char *
vk_machine_get_debug_string (vk_machine_t *mach)
{
	return mach->get_debug_string (mach);
}
