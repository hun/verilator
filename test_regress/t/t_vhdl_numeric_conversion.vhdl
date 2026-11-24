-- DESCRIPTION: Verilator: numeric_std operations on converted std_logic_vector operands
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (a : in std_logic_vector(3 downto 0);
        b : in std_logic_vector(3 downto 0);
        -- unsigned(a) and unsigned(b)
        u_add : out std_logic_vector(3 downto 0);
        u_sub : out std_logic_vector(3 downto 0);
        u_mul : out std_logic_vector(7 downto 0);
        u_div : out std_logic_vector(3 downto 0);
        u_rem : out std_logic_vector(3 downto 0);
        u_mod : out std_logic_vector(3 downto 0);
        u_cmp : out std_logic_vector(5 downto 0);
        u_shift : out std_logic_vector(3 downto 0);
        u_rotate : out std_logic_vector(3 downto 0);
        u_ext : out std_logic_vector(5 downto 0);
        u_int : out integer range 0 to 15;
        u_lit : out std_logic_vector(3 downto 0);
        -- signed(a) and signed(b)
        s_add : out std_logic_vector(3 downto 0);
        s_sub : out std_logic_vector(3 downto 0);
        s_mul : out std_logic_vector(7 downto 0);
        s_div : out std_logic_vector(3 downto 0);
        s_rem : out std_logic_vector(3 downto 0);
        s_mod : out std_logic_vector(3 downto 0);
        s_cmp : out std_logic_vector(5 downto 0);
        s_shift : out std_logic_vector(3 downto 0);
        s_rotate : out std_logic_vector(3 downto 0);
        s_ext : out std_logic_vector(5 downto 0);
        s_int : out integer range -8 to 7;
        s_neg : out std_logic_vector(3 downto 0);
        s_lit : out std_logic_vector(3 downto 0);
        i_mod : out integer range -8 to 7;
        i_rem : out integer range -8 to 7);
end entity t;

architecture rtl of t is
  function bit_of(c : boolean) return std_logic is
  begin
    if c then
      return '1';
    end if;
    return '0';
  end function;
begin
  -- Every operand is a conversion of a std_logic_vector, not a signed or unsigned port
  u_add <= std_logic_vector(unsigned(a) + unsigned(b));
  u_sub <= std_logic_vector(unsigned(a) - unsigned(b));
  u_mul <= std_logic_vector(unsigned(a) * unsigned(b));
  u_div <= std_logic_vector(unsigned(a) / unsigned(b));
  u_rem <= std_logic_vector(unsigned(a) rem unsigned(b));
  u_mod <= std_logic_vector(unsigned(a) mod unsigned(b));
  u_cmp <= (0 => bit_of(unsigned(a) < unsigned(b)),
            1 => bit_of(unsigned(a) <= unsigned(b)),
            2 => bit_of(unsigned(a) > unsigned(b)),
            3 => bit_of(unsigned(a) >= unsigned(b)),
            4 => bit_of(unsigned(a) = unsigned(b)),
            5 => bit_of(unsigned(a) /= unsigned(b)));
  u_shift <= std_logic_vector(shift_left(unsigned(a), 1) or shift_right(unsigned(b), 2));
  u_rotate <= std_logic_vector(rotate_left(unsigned(a), 1) xor rotate_right(unsigned(b), 1));
  u_ext <= std_logic_vector(resize(unsigned(a), 6));
  u_int <= to_integer(unsigned(a));
  u_lit <= std_logic_vector(unsigned(a) + 3 + 2 * to_integer(unsigned(b)));

  s_add <= std_logic_vector(signed(a) + signed(b));
  s_sub <= std_logic_vector(signed(a) - signed(b));
  s_mul <= std_logic_vector(signed(a) * signed(b));
  s_div <= std_logic_vector(signed(a) / signed(b));
  s_rem <= std_logic_vector(signed(a) rem signed(b));
  s_mod <= std_logic_vector(signed(a) mod signed(b));
  s_cmp <= (0 => bit_of(signed(a) < signed(b)),
            1 => bit_of(signed(a) <= signed(b)),
            2 => bit_of(signed(a) > signed(b)),
            3 => bit_of(signed(a) >= signed(b)),
            4 => bit_of(signed(a) = signed(b)),
            5 => bit_of(signed(a) /= signed(b)));
  s_shift <= std_logic_vector(shift_left(signed(a), 1) or shift_right(signed(b), 2));
  s_rotate <= std_logic_vector(rotate_left(signed(a), 1) xor rotate_right(signed(b), 1));
  s_ext <= std_logic_vector(resize(signed(a), 6));
  s_int <= to_integer(signed(a));
  s_neg <= std_logic_vector(-signed(a) + abs signed(b));
  s_lit <= std_logic_vector(signed(a) + 3 - 2 * to_integer(signed(b)));
  -- The same operators on integers, whose remainder and modulus differ in sign; a zero
  -- divisor is an error in VHDL, so it is kept away from them
  process (a, b) is
  begin
    if signed(b) /= 0 then
      i_mod <= to_integer(signed(a)) mod to_integer(signed(b));
      i_rem <= to_integer(signed(a)) rem to_integer(signed(b));
    else
      i_mod <= 0;
      i_rem <= 0;
    end if;
  end process;
end architecture rtl;
