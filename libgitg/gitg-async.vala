/*
 * This file is part of gitg
 *
 * Copyright (C) 2013 - Jesse van den Kieboom
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

namespace Gitg
{

public class Async
{
	public delegate void ThreadFunc() throws Error;

	public static async void thread(ThreadFunc func) throws Error
	{
		SourceFunc callback = thread.callback;
		Error? err = null;

		var t = new Thread<void *>.try("gitg-async", () => {
			try
			{
				func();
			}
			catch (Error e)
			{
				err = e;
			}

			Idle.add((owned)callback);
			return null;
		});

		yield;

		t.join();

		if (err != null)
		{
			throw err;
		}
	}

	public static async void thread_try(ThreadFunc func)
	{
		try
		{
			yield thread(func);
		} catch {}
	}
}

}

// ex:set ts=4 noet
