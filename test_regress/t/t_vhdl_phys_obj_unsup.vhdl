-- DESCRIPTION: Verilator: VHDL run-time physical objects are refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The subset has no run-time physical object (5.14): a physical value exists
-- only while elaborating.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
begin
  proc : process
    variable d : time;
  begin
    d := 10 ns;
    wait;
  end process;
end;
