-- DESCRIPTION: Verilator: VHDL duplicate enumeration literal names
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package enum_pkg is
  type a_t is (a0, a1, shared);
  type b_t is (shared, b1, b2);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.enum_pkg.all;

entity t is
  port (
    pos_o  : out integer;
    eq_o   : out bit;
    case_o : out bit
  );
end;

architecture rtl of t is
  signal sa : a_t := shared;
  signal sb : b_t := shared;
begin
  -- a_t's shared is position 2, b_t's is position 0
  pos_o <= a_t'pos(sa) * 10 + b_t'pos(sb);
  eq_o <= '1' when sa = shared else '0';
  with sa select case_o <= '1' when shared, '0' when others;
end;
