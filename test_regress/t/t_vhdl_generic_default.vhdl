-- DESCRIPTION: Verilator: an input port default that names the entity's own generics
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- open-logic's rate limiter takes its configuration range and the default value of that
-- configuration from its generic period and sample count. Both the type and the default
-- of the port therefore name generics of the same entity, and an instance that leaves the
-- port unconnected needs the default evaluated with the generics it bound.

package pkg is
  function clog2(x : positive) return natural;
end package;

package body pkg is
  function clog2(x : positive) return natural is
    variable v : natural := 0;
    variable r : natural := 1;
  begin
    while r < x loop
      r := r * 2;
      v := v + 1;
    end loop;
    return v;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.pkg.all;

entity t is
  generic (
    Period_g : positive := 5;
    Len_g    : positive := 3
  );
  port (
    clk   : in  std_logic;
    d     : in  std_logic_vector(Len_g - 1 downto 0);
    q     : out std_logic_vector(Len_g - 1 downto 0);
    cfg   : in  std_logic_vector(clog2(Period_g) - 1 downto 0)
                  := std_logic_vector(to_unsigned(Period_g - 1, clog2(Period_g)));
    cfg_o : out std_logic_vector(clog2(Period_g) - 1 downto 0)
  );
end entity;

architecture rtl of t is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      q <= d;
      cfg_o <= cfg;
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Two instances, neither connecting the configuration port, so both take its default
entity t_top is
  port (
    clk   : in  std_logic;
    d_a   : in  std_logic_vector(2 downto 0);
    d_b   : in  std_logic_vector(3 downto 0);
    q_a   : out std_logic_vector(2 downto 0);
    q_b   : out std_logic_vector(3 downto 0);
    cfg_a : out std_logic_vector(2 downto 0);
    cfg_b : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t_top is
begin
  a : entity work.t
    port map (clk => clk, d => d_a, q => q_a, cfg_o => cfg_a);

  b : entity work.t
    generic map (Period_g => 9, Len_g => 4)
    port map (clk => clk, d => d_b, q => q_b, cfg_o => cfg_b);
end architecture;
