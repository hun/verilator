-- DESCRIPTION: Verilator: VHDL static ranges, aggregates and function locals
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity child is
  generic (L : natural);
  port (
    x : in std_logic_vector(L - 1 downto 0);
    y : out std_logic
  );
end;

architecture rtl of child is
begin
  y <= x(0);
end;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  generic (N : natural := 3);
  port (
    a     : in std_logic_vector(3 downto 0);
    b     : in std_logic_vector(3 downto 0);
    o_exp : out std_logic_vector(2**N - 1 downto 0);
    o_div : out std_logic_vector((2**N / 2) - 1 downto 0);
    o_qual : out std_logic_vector(3 downto 0);
    o_func : out std_logic_vector(3 downto 0);
    o_alt : out std_logic_vector(3 downto 0);
    o_pos : out std_logic_vector(3 downto 0);
    o_rng : out std_logic_vector(3 downto 0);
    o_rec : out std_logic_vector(3 downto 0);
    o_child : out std_logic
  );
end;

architecture rtl of t is
  type rec_t is record
    m0 : std_logic;
    m1 : std_logic;
    m2 : std_logic_vector(1 downto 0);
  end record;

  constant CAT : std_logic_vector(3 downto 0) := ("01", "10");
  constant REC : rec_t := (m0 | m1 => '0', m2 => "11");

  function mask (len : natural) return std_logic_vector is
    variable res : std_logic_vector(len - 1 downto 0);
  begin
    res := (0 | 1 => '0', others => '1');
    return res;
  end function;
begin
  o_exp <= std_logic_vector(resize(unsigned(a), 2**N));
  o_div <= a;
  o_qual <= std_logic_vector(unsigned(a) + unsigned'(0 => b(0)));
  o_func <= mask(4) and not a;
  o_alt <= mask(4) xor a;
  o_pos <= CAT xor a;
  o_rec <= REC.m0 & REC.m1 & REC.m2;

  process (a)
    variable v : std_logic_vector(a'range);
  begin
    v := (a'range => '1');
    o_rng <= v and a;
  end process;

  u_child : entity work.child
    generic map (L => 4)
    port map (x => a, y => o_child);
end;
