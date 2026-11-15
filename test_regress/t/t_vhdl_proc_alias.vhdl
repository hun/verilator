-- DESCRIPTION: Verilator: VHDL an alias in a process declarative part
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A process may declare an alias next to its variables (IEEE 1076-2008 6.6), and
-- microwatt's sim_jtag writes one for a signal it uses all over. The alias denotes
-- the same designator, so the value passes through. Every value below is the one
-- GHDL reports.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_proc_alias is
  port (
    a : in  std_logic_vector(3 downto 0);
    o : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t_vhdl_proc_alias is
begin
  proc : process (a)
    alias x is a;
    variable v : std_logic_vector(3 downto 0);
  begin
    v := x;
    o <= v;
  end process;
end architecture;