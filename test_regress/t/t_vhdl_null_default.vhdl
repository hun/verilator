-- DESCRIPTION: Verilator: VHDL default-generic base module with a null array
-- is not elaborated when it is only used through a specialization
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity null_unit is
  generic (
    N : natural := 1
  );
  port (
    -- With the default N=1 this is -1 downto 0, a legal zero-length vector
    a : in  std_logic_vector(N - 2 downto 0);
    y : out std_logic
  );
end entity null_unit;

architecture rtl of null_unit is
begin
  y <= a(0);
end architecture rtl;


library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i : in  std_logic;
    q : out std_logic
  );
end entity t;

architecture rtl of t is
begin
  -- Only the N=2 specialization is elaborated; the default-generic base module
  -- would contain a null array and is never created.
  u : entity work.null_unit
    generic map (N => 2)
    port map (a(0) => i, y => q);
end architecture rtl;
