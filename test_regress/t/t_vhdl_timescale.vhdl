-- DESCRIPTION: Verilator: VHDL delays under a --timescale other than the default
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A VHDL time is absolute, so it must not depend on the time unit, may be finer than
-- the unit, and may exceed 2**32 precision steps.

entity t_vhdl_timescale is
end entity t_vhdl_timescale;

architecture sim of t_vhdl_timescale is
begin
  fine : process
  begin
    wait for 31500 ps;
    report "fine delay done";
    wait;
  end process fine;

  long : process
  begin
    wait for 5 ms;
    std.env.finish;
    wait;
  end process long;
end architecture sim;
