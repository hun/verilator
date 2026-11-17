-- DESCRIPTION: Verilator: VHDL large memory initializer lowers in linear time
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `(others => (others => '0'))` over a large memory. Expanding it into one
-- concatenation per element made the initializer quadratic.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_mem_init is
  port (
    addr : in  integer range 0 to 65535;
    d    : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t_vhdl_mem_init is
  type mem_t is array (0 to 65535) of std_logic_vector(7 downto 0);
  signal mem : mem_t := (others => (others => '0'));
begin
  d <= mem(addr);
end architecture;
