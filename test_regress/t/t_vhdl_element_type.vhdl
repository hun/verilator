-- DESCRIPTION: Verilator: VHDL 'element as a type mark
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `T'element` is the element type of an array, and the name before the attribute
-- may be a type or an object: poc declares a register as `BCDDigits'element`,
-- naming a port. Every value below is the one GHDL and nvc report.

library ieee;
use ieee.std_logic_1164.all;

package elempkg is
  type t_vec is array (natural range <>) of std_logic_vector(3 downto 0);
  constant c : t_vec(1 downto 0) := ("0011", "1100");
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.elempkg.all;

entity t_vhdl_element_type is
  port (
    o : out std_logic_vector(3 downto 0);
    p : out std_logic_vector(3 downto 0);
    n : out integer
  );
end entity;

architecture rtl of t_vhdl_element_type is
  -- An object's element type, as poc names a port
  signal x : c'element;
  -- A type mark's element type
  signal y : t_vec'element;
begin
  -- c(1) is the aggregate's first element, as the index range is 1 downto 0
  x <= c(0);
  y <= c(1);
  o <= x;
  p <= y;
  n <= x'length;
end architecture;
