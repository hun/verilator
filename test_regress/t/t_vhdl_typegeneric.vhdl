-- DESCRIPTION: Verilator: VHDL generic type parameters through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee; use ieee.std_logic_1164.all;
entity mux_g is
  generic (type T);
  port (a : in T; b : in T; sel : in std_logic; y : out T);
end;
architecture rtl of mux_g is
begin
  y <= a when sel = '0' else b;
end;

library ieee; use ieee.std_logic_1164.all;
entity reg_g is
  generic (type T);
  port (clk : in std_logic; din : in T; dout : out T);
end;
architecture rtl of reg_g is
  signal r : T;
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      r <= din;
    end if;
  end process;
  dout <= r;
end;

library ieee; use ieee.std_logic_1164.all;
entity t is
  port (clk : in std_logic; sel : in std_logic;
        a4 : in std_logic_vector(3 downto 0); b4 : in std_logic_vector(3 downto 0);
        a2 : in std_logic_vector(1 downto 0); b2 : in std_logic_vector(1 downto 0);
        y4 : out std_logic_vector(3 downto 0); y2 : out std_logic_vector(1 downto 0));
end;
architecture rtl of t is
  signal m4 : std_logic_vector(3 downto 0);
  signal m2 : std_logic_vector(1 downto 0);
begin
  u_mux4 : entity work.mux_g generic map (T => std_logic_vector(3 downto 0))
    port map (a => a4, b => b4, sel => sel, y => m4);
  u_mux2 : entity work.mux_g generic map (T => std_logic_vector(1 downto 0))
    port map (a => a2, b => b2, sel => sel, y => m2);
  u_reg4 : entity work.reg_g generic map (T => std_logic_vector(3 downto 0))
    port map (clk => clk, din => m4, dout => y4);
  y2 <= m2;
end;
