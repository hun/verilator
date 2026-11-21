-- DESCRIPTION: Verilator: VHDL statement and process diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Invalid choices, conditions, logical operands and sensitivity lists.

entity top is
  port (s : in integer; v : in bit_vector(3 downto 0); x : out integer; b : out bit);
end entity;

architecture rtl of top is
begin
  process (s)
  begin
    case s is
      when 3 downto 5 => x <= 1;
      when others => x <= 0;
    end case;
  end process;

  with s select b <= '1' when 0, '0' when others, '1' when others;

  process (v)
  begin
    if v then
      b <= '1';
    end if;
  end process;

  x <= s and s;

  process (nosuch)
  begin
  end process;

  process (w(0))
    variable w : bit_vector(1 downto 0);
  begin
  end process;
end architecture;
