-- DESCRIPTION: Verilator: VHDL conditional signal/variable assignment
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `x <= a when c else b` and `v := a when c else b` inside a process, plus the
-- `unaffected` waveform, were reported as unsupported.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_cond_assign is
  port (
    clk : in  std_logic;
    en  : in  std_logic;
    sel : in  std_logic;
    a   : in  std_logic_vector(3 downto 0);
    b   : in  std_logic_vector(3 downto 0);
    o   : out std_logic_vector(3 downto 0);
    cnt : out std_logic_vector(3 downto 0);
    k   : out std_logic_vector(3 downto 0)
  );
end entity;

architecture a of t_vhdl_cond_assign is
  signal s    : std_logic_vector(3 downto 0);
  signal keep : std_logic_vector(3 downto 0) := (others => '0');
begin
  process (clk)
    variable v : integer range 0 to 15;
  begin
    if rising_edge(clk) then
      s    <= a when sel = '1' else b;
      keep <= a when en = '1' else unaffected;
      v := to_integer(unsigned(a)) when en = '1' else 0;
      cnt <= std_logic_vector(TO_UNSIGNED(v, 4));
    end if;
  end process;
  o <= s;
  k <= keep;
end architecture;
