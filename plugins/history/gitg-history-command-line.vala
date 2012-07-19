/*
 * This file is part of gitg
 *
 * Copyright (C) 2012 - Jesse van den Kieboom
 *
 * gitg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * gitg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gitg. If not, see <http://www.gnu.org/licenses/>.
 */

namespace GitgHistory
{
	public class CommandLine : Object, GitgExt.CommandLine
	{
		// Do this to pull in config.h before glib.h (for gettext...)
		private const string version = Gitg.Config.VERSION;

		public static bool all;

		private static const OptionEntry[] entries = {
			{"all", 'a', 0, OptionArg.NONE,
			 ref all, N_("Show all history"), null},
			{null}
		};

		public OptionGroup get_option_group()
		{
			var ret = new OptionGroup("history",
			                          N_("Show gitg history options"),
			                          N_("gitg history options"));

			ret.add_entries(entries);
			return ret;
		}
	}
}

// vi:ts=4
