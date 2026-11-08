-- DESCRIPTION: Verilator: VHDL generic map specialization
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity sub is
  generic (N : integer := 4);
  port (
    a : in std_logic_vector(N - 1 downto 0);
    y : out std_logic_vector(N - 1 downto 0)
  );
end;

architecture rtl of sub is
begin
  y <= a;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic_vector(7 downto 0);
    b : in std_logic_vector(1 downto 0);
    c : in std_logic_vector(3 downto 0);
    d : in std_logic_vector(1 downto 0);
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector(1 downto 0);
    w : out std_logic_vector(3 downto 0);
    v : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
  -- Component declaration carries its own generic and generic-dependent port ranges
  component sub is
    generic (N : integer := 4);
    port (
      a : in std_logic_vector(N - 1 downto 0);
      y : out std_logic_vector(N - 1 downto 0)
    );
  end component;
begin
  u1 : entity work.sub generic map (N => 8) port map (a => a, y => y);
  u2 : entity work.sub generic map (N => 2) port map (a => b, y => z);
  -- No generic map: the entity's default N = 4 applies
  u3 : entity work.sub port map (a => c, y => w);
  -- Component-style connection with a generic map
  u4 : sub generic map (N => 2) port map (a => d, y => v);
end;
