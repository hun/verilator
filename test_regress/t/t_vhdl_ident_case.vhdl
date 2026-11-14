-- DESCRIPTION: Verilator: VHDL identifier case folding
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity Mixed_Child is
  generic (G_Width : integer := 4);
  port (
    I_Data : in  std_logic_vector(G_Width - 1 downto 0);
    I_En   : in  std_logic;
    O_Data : out std_logic_vector(G_Width - 1 downto 0);
    O_Cnt  : out integer range 0 to 15
    );
end entity;

architecture rtl of Mixed_Child is
  signal r_Masked : std_logic_vector(G_Width - 1 downto 0);
begin
  -- The internal name is read in another case than it was declared
  r_masked <= i_data when I_EN = '1' else (others => '0');
  o_DATA <= r_Masked;
  o_cnt <= to_integer(unsigned(r_masked));
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    I_Data : in  std_logic_vector(3 downto 0);
    I_En   : in  std_logic;
    O_Data : out std_logic_vector(3 downto 0);
    O_Cnt  : out integer range 0 to 15
    );
end entity;

architecture rtl of t is
begin
  -- Design unit, generic and port formals all written in a different case than
  -- the entity declares them; the testbench drives the top ports in lowercase.
  u : entity work.mixed_child
    generic map (g_width => 4)
    port map (
      i_data => I_Data,
      I_EN   => i_en,
      o_data => O_Data,
      O_cnt  => o_cnt
      );
end architecture;
