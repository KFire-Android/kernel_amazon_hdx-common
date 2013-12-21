/**
 * virtualinput.h
 *
 * Copyright (c) 2012, Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _VIRTUAL_INPUT_H
#define _VIRTUAL_INPUT_H

/*Event Types*/
#define VIRTUAL_EVENT_MOUSE 0
#define VIRTUAL_EVENT_KEYBOARD 1
#define VIRTUAL_EVENT_TOUCH 2

struct virtual_event {
	int type;
	int xcoord;
	int ycoord;
};


#endif
