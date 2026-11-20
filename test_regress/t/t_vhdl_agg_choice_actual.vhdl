-- DESCRIPTION: Verilator: a range-choice aggregate as an unconstrained formal actual
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An aggregate whose only choice is an object's range has no type of its own: the
-- enclosing expression fixes it. Adding it to an unsigned takes the unsigned `ite`
-- out of the overload set (IEEE 1076-2008 9.1), and the choice's bounds give the
-- aggregate its width.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package ovl is
  function ite(cond : boolean; a, b : boolean) return boolean;
  function ite(cond : boolean; a, b : integer) return integer;
  function ite(cond : boolean; a, b : std_logic) return std_logic;
  function ite(cond : boolean; a, b : bit_vector) return bit_vector;
  function ite(cond : boolean; a, b : std_logic_vector) return std_logic_vector;
  function ite(cond : boolean; a, b : unsigned) return unsigned;
  function ite(cond : boolean; a, b : signed) return signed;
  function ite(cond : boolean; a, b : character) return character;
end package;

package body ovl is
  function ite(cond : boolean; a, b : boolean) return boolean is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : integer) return integer is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : std_logic) return std_logic is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : bit_vector) return bit_vector is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : std_logic_vector) return std_logic_vector is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : unsigned) return unsigned is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : signed) return signed is
  begin
    if cond then return a; else return b; end if;
  end function;

  function ite(cond : boolean; a, b : character) return character is
  begin
    if cond then return a; else return b; end if;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.ovl.all;

entity t is
  port (
    clk : in  std_logic;
    add : in  unsigned(3 downto 0);
    a   : in  std_logic_vector(3 downto 0);
    y   : out unsigned(3 downto 0);
    sz  : out signed(4 downto 0);
    so  : out signed(4 downto 0);
    f   : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
  signal Cnt : unsigned(3 downto 0) := (others => '0');
  signal S0  : signed(4 downto 0) :=
    ite(true, (4 downto 0 => '0'), ('0' & (3 downto 0 => '1')));
  signal S1  : signed(4 downto 0) :=
    ite(false, (4 downto 0 => '0'), ('0' & (3 downto 0 => '1')));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      Cnt <= Cnt + ite(Cnt(3) = '0', (Cnt'range => '1'), to_unsigned(4, 4)) + add;
    end if;
  end process;

  y  <= Cnt;
  sz <= S0;
  so <= S1;
  f  <= ite(a(3) = '1', (a'range => '1'), (a'range => '0'));
end architecture;
