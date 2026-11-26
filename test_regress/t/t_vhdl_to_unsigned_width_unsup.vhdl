-- DESCRIPTION: Verilator: to_unsigned with a run-time width and no target has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Without a sized target the width of to_unsigned must be constant.

library ieee;
use ieee.numeric_std.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable n : natural;
  begin
    if to_unsigned(3, n) = to_unsigned(3, n) then o <= '1'; end if;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
