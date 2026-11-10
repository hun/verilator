-- DESCRIPTION: Verilator: VHDL bit-vector generics
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vg_unit is
  generic (
    width : positive;
    seed  : std_ulogic_vector(width - 1 downto 0)
  );
  port (
    idx    : in  integer range 0 to 7;
    o      : out std_ulogic;
    lo_o   : out integer;
    hi_o   : out integer;
    len_o  : out integer;
    word_o : out std_ulogic_vector(7 downto 0)
  );
end entity vg_unit;

architecture rtl of vg_unit is
  -- A vector generic initializes a signal and is read as an expression
  signal state : std_ulogic_vector(width - 1 downto 0) := seed;
begin

  assert unsigned(seed) /= 0 report "seed must be non-zero" severity failure;

  o      <= state(idx);
  lo_o   <= seed'low;
  hi_o   <= seed'high;
  len_o  <= seed'length;
  word_o <= state(seed'length - 1 downto 0);

end architecture rtl;


library ieee;
use ieee.std_logic_1164.all;

-- Forwards a vector generic and relies on its own aggregate default
entity vg_pass is
  generic (
    g_seed : std_ulogic_vector(7 downto 0) := (others => '1')
  );
  port (
    idx    : in  integer range 0 to 7;
    o      : out std_ulogic;
    lo_o   : out integer;
    hi_o   : out integer;
    len_o  : out integer;
    word_o : out std_ulogic_vector(7 downto 0)
  );
end entity vg_pass;

architecture rtl of vg_pass is
begin
  u : entity work.vg_unit
    generic map (
      width => 8,
      seed  => g_seed
    )
    port map (
      idx    => idx,
      o      => o,
      lo_o   => lo_o,
      hi_o   => hi_o,
      len_o  => len_o,
      word_o => word_o
    );
end architecture rtl;


library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- u_unsigned is a vector subtype from numeric_std-2008
entity uv_unit is
  generic (
    g : u_unsigned(3 downto 0) := (others => '1')
  );
  port (
    q : out u_unsigned(3 downto 0)
  );
end entity uv_unit;

architecture rtl of uv_unit is
begin
  q <= g;
end architecture rtl;


library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    idx   : in  integer range 0 to 7;
    o0    : out std_ulogic;
    o1    : out std_ulogic;
    o2    : out std_ulogic;
    w0    : out std_ulogic_vector(7 downto 0);
    w1    : out std_ulogic_vector(7 downto 0);
    w2    : out std_ulogic_vector(7 downto 0);
    lo0   : out integer;
    hi0   : out integer;
    len0  : out integer;
    lo2   : out integer;
    hi2   : out integer;
    len2  : out integer;
    w3    : out u_unsigned(3 downto 0);
    w4    : out u_unsigned(3 downto 0)
  );
end entity t;

architecture rtl of t is
begin

  -- Named and positional generic maps, plus a forwarded default
  u0 : entity work.vg_unit
    generic map (
      width => 8,
      seed  => "10100101"
    )
    port map (
      idx    => idx,
      o      => o0,
      lo_o   => lo0,
      hi_o   => hi0,
      len_o  => len0,
      word_o => w0
    );

  u1 : entity work.vg_unit
    generic map (8, "11110000")
    port map (
      idx    => idx,
      o      => o1,
      lo_o   => open,
      hi_o   => open,
      len_o  => open,
      word_o => w1
    );

  u2 : entity work.vg_pass
    port map (
      idx    => idx,
      o      => o2,
      lo_o   => lo2,
      hi_o   => hi2,
      len_o  => len2,
      word_o => w2
    );

  -- u_unsigned generic with an aggregate default and with a literal map
  u3 : entity work.uv_unit
    port map (q => w3);

  u4 : entity work.uv_unit
    generic map (g => "1010")
    port map (q => w4);

end architecture rtl;
