-- DESCRIPTION: Verilator: VHDL real element of an aggregate that is not a static value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type rv is array (0 to 1) of real;
begin
  process
    variable r : real := 0.5;
  begin
    if rv'(r, 1.0) = rv'(0.5, 1.0) then
      o <= '1';
    else
      o <= '0';
    end if;
    wait;
  end process;
end architecture rtl;
