-- DESCRIPTION: Verilator: a sequential waveform with several elements has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;
architecture a of t is
  signal clk : bit;
begin
  process
  begin
    clk <= '1' after 5 ns, '0' after 10 ns;
    wait;
  end process;
end architecture;
