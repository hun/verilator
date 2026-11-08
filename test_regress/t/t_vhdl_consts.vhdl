-- DESCRIPTION: Verilator: VHDL package constants and generic defaults
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package pkg is
  constant W : integer := 4;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.pkg.all;

entity t is
  generic (G : integer := 8);
  port (
    a : in std_logic_vector(W - 1 downto 0);
    b : in std_logic_vector(G - 1 downto 0);
    n : out integer;
    y : out std_logic_vector(G - 1 downto 0)
  );
end;

architecture rtl of t is
  constant C : integer := 2;
begin
  n <= C + W;
  y <= b;
end;
