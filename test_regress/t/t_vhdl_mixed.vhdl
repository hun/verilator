-- DESCRIPTION: Verilator: VHDL design instantiated from SystemVerilog in one invocation
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- One VHDL level below the entity the SystemVerilog top instantiates, so the VHDL
-- hierarchy under a SystemVerilog top is elaborated too
entity mixed_leaf is
  port (
    clk : in std_logic;
    en  : in std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of mixed_leaf is
  signal s : unsigned(3 downto 0) := (others => '0');
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      if en = '1' then
        s <= s + 1;
      end if;
    end if;
  end process;
  q <= std_logic_vector(s);
end;

entity mixed_dut is
  port (
    clk : in std_logic;
    en  : in std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of mixed_dut is
begin
  u_leaf : mixed_leaf port map (
    clk => clk,
    en  => en,
    q   => q
  );
end;
