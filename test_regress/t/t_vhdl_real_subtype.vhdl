-- DESCRIPTION: Verilator: VHDL subtype of real that carries a range constraint
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package real_pkg is
  subtype unit_real_t is real range 0.0 to 1.0;
  subtype flag_int_t is integer range 0 to 1;
  function flag_of(cond : boolean) return flag_int_t;
end package;

library ieee;
use ieee.std_logic_1164.all;

package body real_pkg is
  function flag_of(cond : boolean) return flag_int_t is
  begin
    if cond then
      return 1;
    end if;
    return 0;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.real_pkg.all;

entity t is
  port (
    i_a     : in  std_logic;
    o_bit   : out integer range 0 to 1;
    o_above : out integer range 0 to 1
    );
end entity;

library ieee;
use ieee.std_logic_1164.all;
use work.real_pkg.all;

architecture rtl of t is
  constant half : unit_real_t := 0.5;
  -- A comparison of the constrained real subtype, folded at elaboration
  constant above_quarter : integer range 0 to 1 := flag_of(half > 0.25);
begin
  o_bit <= flag_of(i_a = '1');
  o_above <= above_quarter;
end architecture;
